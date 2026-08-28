#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include "config.h"
#include "light_control.h"

#include "connection_stack_manager.h"
#include "web_server.h"
#include "mqtt_handler.h"
#include "knx_handler.h"
#include "homespan_handler.h"

AppSettings settings;
static Preferences prefs;

bool ledOn = false;           // LED on/off state (defined in main.cpp)
int currentPWM = settings.lastPWM;  // Current PWM brightness value 0-255

void loadSettings() {
    // On a fresh/erased flash the "dimmer" namespace doesn't exist yet, and a
    // read-only open logs "nvs_open failed: NOT_FOUND". Opening read-write
    // once creates the namespace so the read-only open below always succeeds.
    prefs.begin("dimmer", false);
    prefs.end();

    prefs.begin("dimmer", true);
    settings.ssid = prefs.getString("ssid", "");
    settings.password = prefs.getString("pass", "");
    settings.minBrightness = prefs.getInt("minB", 5);
    settings.maxBrightness = prefs.getInt("maxB", 255);
    settings.dimSpeed = prefs.getInt("speed", 15);
    settings.lastPWM = prefs.getInt("lastPwm", 128);
    
    settings.mqttEnabled = prefs.getBool("mqttEn", false);
    settings.mqttServer = prefs.getString("mqttSrv", "");
    settings.mqttPort = prefs.getInt("mqttPort", 1883);
    settings.mqttUser = prefs.getString("mqttUser", "");
    settings.mqttPass = prefs.getString("mqttPass", "");
    settings.mqttTopicBase = prefs.getString("mqttTopic", "Squeeze/led");
    settings.homespanEnabled = prefs.getBool("hsEn", false);
    settings.homespanDeviceId = prefs.getString("hsDevId", "");
    settings.knxEnabled = prefs.getBool("knxEn", false);

    settings.wifiRadioOff = prefs.getBool("wifiOff", false);
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

    prefs.putBool("mqttEn", settings.mqttEnabled);
    prefs.putString("mqttSrv", settings.mqttServer);
    prefs.putInt("mqttPort", settings.mqttPort);
    prefs.putString("mqttUser", settings.mqttUser);
    prefs.putString("mqttPass", settings.mqttPass);
    prefs.putString("mqttTopic", settings.mqttTopicBase);
    prefs.putBool("hsEn", settings.homespanEnabled);
    prefs.putString("hsDevId", settings.homespanDeviceId);
    prefs.putBool("knxEn", settings.knxEnabled);

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

    // Single-Connection-Stack Architecture:
    // Only ONE connection stack runs at a time. Choose one of:
    //   1) Web Server (shares WiFi)
    //   2) HomeSpan via MQTT
    //   3) KNX IP Interface
    //   4) MQTT Broker Client only
    //
    // If the settings don't select exactly one stack (or the Wi-Fi radio
    // is switched off), this stays in NONE state and does nothing.
    ConnectionStackManager::startConfiguredStack();
}

void loop() {
    // Handle the active connection stack
    switch (ConnectionStackManager::getConnectionState()) {
        case ConnectionState::WEB_SERVER:
            handleWebServer();
            break;
        case ConnectionState::HOMESPAN:
            handleHomeSpan();
            break;
        case ConnectionState::MQTT_ONLY:
            handleMQTTBrokerClient();
            break;
        default:
            break;
    }

    // Handle button input (shared across all stacks)
    handleButton();

    // Diagnostic: prints free heap every 10s. Useful for confirming
    // whether memory pressure is contributing to issues.
    static unsigned long lastHeapLog = 0;
    if (millis() - lastHeapLog > 10000) {
        lastHeapLog = millis();
        Serial.printf("[diag] free heap: %u bytes\n", ESP.getFreeHeap());
    }
}
