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
#define RAPID_PRESS_COUNT       10
#define RAPID_PRESS_WINDOW_MS  3000
#define WIFI_RETRY_INTERVAL_MS 30000

// Settings persisted in NVS (Preferences). This is deliberately barebones:
// only Wi-Fi + dimming + brightness memory. When a connection stack
// (MQTT / HomeSpan / KNX) is reintroduced, add its settings fields here,
// plus load/save lines in main.cpp's loadSettings()/saveSettings(), plus
// form fields in data/settings.html.
struct AppSettings {
    String ssid = "";
    String password = "";

    int minBrightness = 5;
    int maxBrightness = 255;
    int dimSpeed = 15;
    int lastPWM = 128;      // brightness memory - restored on boot

    bool wifiRadioOff = false;
};

extern AppSettings settings;
extern bool ledOn;           // LED on/off state (defined in main.cpp)
extern int currentPWM;       // Current PWM brightness value 0-255 (defined in main.cpp)

void loadSettings();
void saveSettings();

#endif
