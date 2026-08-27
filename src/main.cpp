#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include "config.h"
#include "light_control.h"

#include "connection_stack_manager.h"
#include "web_server.h"
#include "mqtt_handler.h"
#include "knx_handler.h"

AppSettings settings;
static Preferences prefs;

void loadSettings() {
    prefs.begin("dimmer", true);
    settings.ssid = prefs.getString("ssid", "");
    settings.password = prefs.getString("pass", "");
    settings.minBrightness = prefs.getInt("minB", 5);
    settings.maxBrightness = prefs.getInt("maxB", 255);
    settings.dimSpeed = prefs.getInt("speed", 15);
    settings.lastPWM = prefs.getInt("lastPwm", 128);
    settings.ledOn = prefs.getBool("ledOn", false);
    
    settings.mqttEnabled = prefs.getBool("mqttEn", false);
    settings.mqttServer = prefs.getString("mqttSrv", "");
    settings.mqttPort = prefs.getInt("mqttPort", 1883);
    settings.mqttUser = prefs.getString("mqttUser", "");
    settings.mqttPass = prefs.getString("mqttPass", "");
    settings.mqttTopicBase = prefs.getString("mqttTopic", "Squeeze/led");

    settings.matterEnabled = prefs.getBool("matterEn", true);
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

    prefs.putBool("matterEn", settings.matterEnabled);
    prefs.putBool("wifiOff", settings.wifiRadioOff);
    prefs.end();
}

void setup() {
    Serial.begin(115200);
    loadSettings();
    initLightControl();

    // Single-Connection-Stack Architecture:
    // Only ONE connection stack runs at a time. Choose one of:
    //   1) Web Server + Matter (shares WiFi)
    //   2) KNX IP Interface
    //   3) MQTT Broker Client only
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
        case ConnectionState::MQTT_ONLY:
            handleMQTTBrokerClient();
            break;
        case ConnectionState::KNX:
            handleKNX();
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
