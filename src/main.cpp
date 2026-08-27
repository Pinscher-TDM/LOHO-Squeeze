#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include "config.h"
#include "light_control.h"

#include "connection_stack_manager.h"
#include "web_server.h"
#include "mqtt_handler.h"

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
    // To switch stacks, setConnectionState() must be called before setup().
    ConnectionState initialState = ConnectionState::NONE;
    
    if (!settings.wifiRadioOff && settings.matterEnabled && !settings.mqttEnabled) {
        // Stack 1: Web Server + Matter (default)
        initialState = ConnectionState::WEB_SERVER;
    } else if (!settings.wifiRadioOff && settings.mqttEnabled && !settings.matterEnabled) {
        // Stack 3: MQTT Broker Client only
        initialState = ConnectionState::MQTT_ONLY;
    }
    
    ConnectionStackManager::setConnectionState(initialState);

    // Initialize the selected stack
    if (initialState == ConnectionState::WEB_SERVER) {
        Serial.println("[STACK] Starting Web Server + Matter stack...");
        initWebServer(settings);
    } else if (initialState == ConnectionState::MQTT_ONLY) {
        Serial.println("[STACK] Starting MQTT Broker Client only stack...");
        initMQTTBrokerClient(settings);
    }

    // If no stack was selected, stay in NONE state and do nothing
}

void loop() {
    // Handle the active connection stack
    if (ConnectionStackManager::isAnyStackActive()) {
        StackType active = ConnectionStackManager::getActiveStackType();
        
        switch (active) {
            case StackType::WEB_SERVER_STACK:
                handleWebServer();
                break;
            case StackType::MQTT_BROKER_STACK:
                handleMQTTBrokerClient();
                break;
            default:
                // KNX stack not yet implemented
                break;
        }
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
