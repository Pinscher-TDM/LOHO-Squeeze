#include "homespan_handler.h"
#include "mqtt_handler.h"
#include "light_control.h"

extern bool ledOn;
extern int currentPWM;

static bool homespanActive = false;

// HomeSpan publishes state to "hs/<device_id>/light" and subscribes to
// "hs/+/light/set" for on/off/brightness commands. We mirror this with our
// own topic base (settings.mqttTopicBase) so the user can configure it in
// settings.html, but we also publish to the standard HomeSpan topics when
// enabled.

void initHomeSpan() {
    if (!homespanActive || !settings.mqttEnabled) return;

    // Publish current state to HomeSpan topic
    String topic = "hs/" + settings.homespanDeviceId + "/light";
    mqtt_publish(topic.c_str(), ledOn ? "ON" : "OFF");
    mqtt_publish((topic + "/brightness").c_str(), String(currentPWM).c_str());

    // Subscribe to HomeSpan control topics (wildcard for any device)
    mqtt_subscribe("hs/+/light/set", [](char* payload, unsigned int len) {
        if (!payload || len == 0) return;
        String msg(payload, len);
        if (msg == "ON") {
            ledOn = true;
            currentPWM = settings.maxBrightness;
        } else if (msg == "OFF") {
            ledOn = false;
            currentPWM = 0;
            // Assume it's a brightness value (0-255)
            int level = msg.toInt();
            currentPWM = constrain(level, settings.minBrightness, settings.maxBrightness);
            if (!ledOn) ledOn = true;
        }
        applyPWM(true);  // publish state change
    });

    homespanActive = true;
}

void handleHomeSpan() {
    if (!homespanActive) return;
    mqtt_handle();  // process incoming HomeSpan messages via the shared MQTT client
}

void publishHomeSpanState() {
    if (!homespanActive || !settings.mqttEnabled) return;
    String topic = "hs/" + settings.homespanDeviceId + "/light";
    mqtt_publish(topic.c_str(), ledOn ? "ON" : "OFF");
    mqtt_publish((topic + "/brightness").c_str(), String(currentPWM).c_str());
}
