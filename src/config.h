#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

#define LED_PIN       3
#define BUTTON_PIN    4

#define DEVICE_HOSTNAME "Squeeze"

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

    bool homespanEnabled = false;
    String homespanDeviceId = "";

    bool knxEnabled = false;
    bool wifiRadioOff = false;
    int lastPWM = 128;
};

extern AppSettings settings;
extern bool ledOn;           // LED on/off state (defined in main.cpp)
extern int currentPWM;       // Current PWM brightness value 0-255 (defined in main.cpp)

void loadSettings();
void saveSettings();

#endif