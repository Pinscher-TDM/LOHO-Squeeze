#include "mqtt_handler.h"
#include "config.h"
#include "light_control.h"
#include "web_server.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

static WiFiClient wifiClient;
static PubSubClient mqttClient(wifiClient);
static unsigned long lastMqttAttempt = 0;
static String mqttClientId;

void mqttCallback(char* topic, byte* payload, unsigned int length) {
    String msg;
    for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
    msg.trim();

    String setTopic = settings.mqttTopicBase + "/set";
    String briTopic = settings.mqttTopicBase + "/brightness/set";

    if (String(topic) == setTopic) {
        ledOn = msg.equalsIgnoreCase("ON");
        applyPWM(true, true);
        saveSettings();
    } else if (String(topic) == briTopic) {
        currentPWM = constrain(msg.toInt(), settings.minBrightness, settings.maxBrightness);
        ledOn = true;
        applyPWM(true, true);
        saveSettings();
    }
}

void setupMQTT() {
    if (!settings.mqttEnabled || settings.mqttServer.length() == 0) return;
    uint64_t chipId = ESP.getEfuseMac();
    char idBuf[24];
    snprintf(idBuf, sizeof(idBuf), "Squeeze-LED-%04X", (uint16_t)(chipId & 0xFFFF));
    mqttClientId = String(idBuf);

    mqttClient.setServer(settings.mqttServer.c_str(), settings.mqttPort);
    mqttClient.setCallback(mqttCallback);
}

void reconnectMQTT() {
    if (WiFi.status() != WL_CONNECTED || !settings.mqttEnabled) return;
    
    bool connected = settings.mqttUser.length() > 0 ? 
        mqttClient.connect(mqttClientId.c_str(), settings.mqttUser.c_str(), settings.mqttPass.c_str()) : 
        mqttClient.connect(mqttClientId.c_str());

    if (connected) {
        mqttClient.subscribe((settings.mqttTopicBase + "/set").c_str());
        mqttClient.subscribe((settings.mqttTopicBase + "/brightness/set").c_str());
        publishMQTTState();
        publishHADiscovery();
    }
}

void handleMQTT() {
    if (!settings.mqttEnabled || settings.wifiRadioOff) return;
    if (!mqttClient.connected()) {
        if (millis() - lastMqttAttempt > MQTT_RETRY_MS) {
            lastMqttAttempt = millis();
            reconnectMQTT();
        }
    } else {
        mqttClient.loop();
    }
}

void publishMQTTState() {
    if (!settings.mqttEnabled || !mqttClient.connected()) return;
    mqttClient.publish((settings.mqttTopicBase + "/state").c_str(), ledOn ? "ON" : "OFF", true);
    mqttClient.publish((settings.mqttTopicBase + "/brightness/state").c_str(), String(currentPWM).c_str(), true);
}

// MQTT Broker Client stack lifecycle - MQTT without the web server. Matter
// stays off because setupMatter() no-ops when settings.matterEnabled is false.
static bool mqttStackActive = false;

void initMQTTBrokerClient() {
    if (mqttStackActive) return;
    setupWiFi();  // this stack still needs the Wi-Fi link
    setupMQTT();
    mqttStackActive = true;
}

void shutdownMQTTBrokerClient() {
    if (!mqttStackActive) return;
    Serial.println("[MQTT] Shutting down MQTT broker client stack...");
    mqttClient.disconnect();
    mqttStackActive = false;
}

bool isMQTTBrokerClientActive() {
    return mqttStackActive;
}

void handleMQTTBrokerClient() {
    if (!mqttStackActive) return;
    handleMQTT();
}

void publishHADiscovery() {
    if (!settings.mqttEnabled || !mqttClient.connected()) return;

    StaticJsonDocument<512> doc;
    doc["name"] = "Squeeze Light";
    doc["unique_id"] = mqttClientId;
    doc["cmd_t"] = settings.mqttTopicBase + "/set";
    doc["stat_t"] = settings.mqttTopicBase + "/state";
    doc["bri_cmd_t"] = settings.mqttTopicBase + "/brightness/set";
    doc["bri_stat_t"] = settings.mqttTopicBase + "/brightness/state";
    
    JsonObject dev = doc.createNestedObject("device");
    dev["identifiers"][0] = mqttClientId;
    dev["name"] = "LOHO Squeeze Light";

    String payload;
    serializeJson(doc, payload);
    mqttClient.publish(("homeassistant/light/" + mqttClientId + "/config").c_str(), payload.c_str(), true);
}