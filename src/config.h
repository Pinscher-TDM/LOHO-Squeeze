#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <Preferences.h>

#define LED_PIN       3
#define BUTTON_PIN    4

// Hostname includes the lamp ID for multi-device support on a network
static char DEVICE_HOSTNAME[17]; // "LOHO-Squeeze-XXXX\0" = 16 chars + null
static void initHostname() {
    snprintf(DEVICE_HOSTNAME, sizeof(DEVICE_HOSTNAME), "LOHO-Squeeze-%04X", ESP.getEfuseMac() & 0xFFFF);
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