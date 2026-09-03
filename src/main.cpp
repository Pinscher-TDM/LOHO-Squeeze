#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include "config.h"
#include "light_control.h"
#include "connection_stack_manager.h"
#include "web_server.h"

AppSettings settings;
static Preferences prefs;

bool ledOn = false;                  // LED on/off state
int currentPWM = settings.lastPWM;   // Current PWM brightness value 0-255

void loadSettings() {
    // On a fresh/erased flash the "dimmer" namespace doesn't exist yet, and
    // a read-only open logs "nvs_open failed: NOT_FOUND". Opening
    // read-write once creates the namespace so the read-only open below
    // always succeeds.
    prefs.begin("dimmer", false);
    prefs.end();

    prefs.begin("dimmer", true);
    settings.ssid = prefs.getString("ssid", "");
    settings.password = prefs.getString("pass", "");
    settings.minBrightness = prefs.getInt("minB", 5);
    settings.maxBrightness = prefs.getInt("maxB", 255);
    settings.dimSpeed = prefs.getInt("speed", 15);
    settings.lastPWM = prefs.getInt("lastPwm", 128);
    settings.wifiRadioOff = prefs.getBool("wifiOff", false);

    // Brightness + on/off memory - restore exactly how the light was left.
    ledOn = prefs.getBool("ledOn", false);
    currentPWM = settings.lastPWM;
    prefs.end();
}

void saveSettings() {
    prefs.begin("dimmer", false);
    prefs.putString("ssid", settings.ssid);
    prefs.putString("pass", settings.password);
    prefs.putInt("minB", settings.minBrightness);
    prefs.putInt("maxB", settings.maxBrightness);
    prefs.putInt("speed", settings.dimSpeed);
    prefs.putInt("lastPwm", currentPWM);
    prefs.putBool("ledOn", ledOn);
    prefs.putBool("wifiOff", settings.wifiRadioOff);
    prefs.end();
}

void setup() {
    Serial.begin(115200);

    // Set WiFi mode to station (STA) so we can connect and use NVS
    WiFi.mode(WIFI_STA);

    delay(100);  // Give NVS time to initialize after boot

    loadSettings();
    initLightControl();

    // Connect to Wi-Fi with saved credentials
    if (settings.ssid.length() > 0) {
        Serial.print("Connecting to ");
        Serial.println(settings.ssid);
        WiFi.begin(settings.ssid.c_str(), settings.password.c_str());
        // C3 PCB-antenna fix: full TX power distorts the signal on many
        // boards - cap it (must run after the radio starts).
        WiFi.setTxPower(WIFI_POWER_8_5dBm);

        while (WiFi.status() != WL_CONNECTED) {
            delay(500);
            Serial.print(".");
        }
        Serial.println("\nWi-Fi connected.");
    } else {
        Serial.println("No Wi-Fi credentials found - using standalone mode.");
    }

    // Single-connection-stack architecture (see connection_stack_manager.h):
    // only one connection stack runs at a time. This barebones build only
    // has WEB_SERVER.
    ConnectionStackManager::startConfiguredStack();
}

void loop() {
    // Handle the active connection stack
    switch (ConnectionStackManager::getConnectionState()) {
        case ConnectionState::WEB_SERVER:
            handleWebServer();
            break;
        // TODO(stacks): add cases here as other connection stacks (MQTT,
        // HomeSpan, KNX) are reintroduced - see connection_stack_manager.h.
        default:
            break;
    }

    // Handle button input (shared across all stacks)
    handleButton();

    // Diagnostic: prints free heap every 10s.
    static unsigned long lastHeapLog = 0;
    if (millis() - lastHeapLog > 10000) {
        lastHeapLog = millis();
        Serial.printf("[diag] free heap: %u bytes\n", ESP.getFreeHeap());
    }
}
