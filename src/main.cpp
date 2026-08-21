#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include "config.h"
#include "light_control.h"

#include "mqtt_handler.h"
#include "web_server.h"

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

    if (!settings.wifiRadioOff) {
        setupWiFi();
        initWebServer();
        setupMQTT();
    } else {
        WiFi.mode(WIFI_OFF);
    }
}

void loop() {
    if (!settings.wifiRadioOff) {
        handleWebServer();
        // BUG FIX: setupMQTT() was called in setup(), but handleMQTT() -
        // which actually connects and processes messages - was never
        // called anywhere. MQTT was effectively dead code before this.
        handleMQTT();
    }
    handleButton();
}