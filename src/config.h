#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <Preferences.h>

#define LED_PIN       3
#define BUTTON_PIN    4

// Hostname includes the lamp ID for multi-device support on a network.
//
// BUG FIX: this used to be a `static char DEVICE_HOSTNAME[17]` paired with an
// initHostname() that *nothing ever called*, so every use site
// (WiFi.setHostname, MDNS.begin) received an empty string. Two further
// problems made it unfixable as written: `static` at file scope in a header
// gives every .cpp its own private copy, so initialising it from one
// translation unit would not have helped the others; and the buffer was one
// byte short - "LOHO-Squeeze-" is 13 chars + 4 hex digits + NUL = 18 - so the
// last hex digit was silently truncated.
//
// A function-local static has exactly one instance across the program and
// initialises itself on first use, so it cannot be forgotten.
inline const char* deviceHostname() {
    static char name[18] = {0};
    if (name[0] == '\0') {
        snprintf(name, sizeof(name), "LOHO-Squeeze-%04X",
                 (unsigned)(ESP.getEfuseMac() & 0xFFFF));
    }
    return name;
}

#define PWM_FREQ      20000 
#define PWM_RES       10    
#define GAMMA         2.2   

#define DEBOUNCE_MS       50
#define HOLD_MS          400
#define MQTT_RETRY_MS     5000 
#define RAPID_PRESS_COUNT       10
#define RAPID_PRESS_WINDOW_MS  3000
#define WIFI_RETRY_INTERVAL_MS 30000

struct AppSettings {
    String ssid = "";
    String password = "";
    int minBrightness = 5;
    int maxBrightness = 255;
    int dimSpeed = 15;
    
    bool mqttEnabled = false;
    String mqttServer = "";
    int mqttPort = 1883;
    String mqttUser = "";
    String mqttPass = "";
    String mqttTopicBase = "Squeeze/led";
    
    bool matterEnabled = true;
    bool wifiRadioOff = false;
    int lastPWM = 128;
    bool ledOn = false;
};

extern AppSettings settings;

void loadSettings();
void saveSettings();

#endif