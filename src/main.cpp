#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include "config.h"
#include "light_control.h"

#include "mqtt_handler.h"
#include "web_server.h"
#include "discovery.h"
#include "matter_handler.h"

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
    Serial.println("LOHO-Squeeze booting...");
    loadSettings();
    initLightControl();

    if (!settings.wifiRadioOff) {
        setupWiFi();
        // Initialize discovery (hostname + UDP socket) after WiFi is ready
        initDiscovery();
        initWebServer();
        setupMQTT();
        // Defer Matter until web server routes are registered to avoid CLUSTERS
        // KeyError. This comment described the intent, but setupMatter() was
        // actually being called from inside setupWiFi() above - i.e. before the
        // web server existed. Now it genuinely runs last.
        setupMatter();
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

        // BUG FIX: handleDiscovery() was declared in web_server.h but never
        // defined or called, so inbound announcements were never read and the
        // peer list was always empty - discovery only ever transmitted.
        handleDiscovery();

        // Periodic presence broadcast (every 60s) so other lamps can discover us
        static unsigned long lastBroadcast = 0;
        if (millis() - lastBroadcast >= 60000UL) {
            lastBroadcast = millis();
            broadcastPresence(settings.ssid.c_str(), getLampId());
        }
    }
    handleButton();
}