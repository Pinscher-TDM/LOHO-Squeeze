#include "mqtt_handler.h"
#include "connection_stack_manager.h"

// MQTT Broker Client Stack Implementation
// This stack connects only as a client to an MQTT broker - no publishing, just subscribing
// Memory-efficient: no HA discovery publishing, minimal static allocations

static void mqttCallback(char* topic, byte* payload, unsigned int length) {
    String msg;
    for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
    msg.trim();

    String setTopic = settings.mqttTopicBase + "/set";
    String briTopic = settings.mqttTopicBase + "/brightness/set";

    if (String(topic) == setTopic) {
        ledOn = msg.equalsIgnoreCase("ON");
        applyPWM(true, false);  // publish to MQTT too; don't re-echo back into Matter
        saveSettings();
    } else if (String(topic) == briTopic) {
        currentPWM = constrain(msg.toInt(), settings.minBrightness, settings.maxBrightness);
        ledOn = true;
        applyPWM(true, false);
        saveSettings();
    }
}

void initMQTTBrokerClient(const MQTTConfig& config) {
    // Only initialize if not already active (idempotent)
    if (ConnectionStackManager::isMQTTBrokerClientActive()) return;

    settings = config;  // Store configuration for later use

    mqttClient.setServer(settings.server.c_str(), settings.port);
    mqttClient.setCallback(mqttCallback);
}

void shutdownMQTTBrokerClient() {
    if (!ConnectionStackManager::isMQTTBrokerClientActive()) return;

    Serial.println("[MQTT] Shutting down MQTT broker client stack...");
    mqttClient.disconnect();
    Serial.println("[MQTT] Stack shutdown complete");
}

bool ConnectionStackManager::isMQTTBrokerClientActive() {
    return mqttClient.connected();
}

void handleMQTTBrokerClient() {
    if (!ConnectionStackManager::isMQTTBrokerClientActive()) return;

    // Reconnect if disconnected
    if (!mqttClient.connected()) {
        if (millis() - lastMqttAttempt > MQTT_RETRY_MS) {
            lastMqttAttempt = millis();
            reconnectMQTT();
        }
    } else {
        mqttClient.loop();
    }
}
