#include "knx_handler.h"
#include "config.h"
#include "light_control.h"
#include "mqtt_handler.h"

extern bool ledOn;
extern int currentPWM;

static bool knxActive = false;
static String knxTopicBase = "";

// KNX IP Interface communicates via MQTT. The library expects topics like:
//   knx/<group>/1/0/on    - ON command (channel 1, dimmer object 0)
//   knx/<group>/1/0/state - state report (we publish here)
// We use a configurable topic base from settings so users can point it to
// their KNX installation.

void initKNX() {
    if (!knxActive || !settings.mqttEnabled) return;

    // Use the configured MQTT topic base, prefixing with "knx/"
    knxTopicBase = settings.mqttTopicBase + "/knx";

    // Publish initial state to KNX (ON/OFF and brightness)
    String onOff = ledOn ? "1" : "0";
    String onTopic = knxTopicBase + "/" + onOff + "/0/on";
    mqtt_publish(onTopic.c_str(), "1");

    // Subscribe to ON command topic
    mqtt_subscribe((knxTopicBase + "/+/0/on").c_str(), [](char* payload, unsigned int len) {
        if (len == 1 && payload[0] >= '0' && payload[0] <= '9') {
            int onOff = payload[0] - '0';
            if (onOff == 1) {
                ledOn = true;
                currentPWM = settings.maxBrightness;
            } else {
                // Any other value is treated as OFF
                ledOn = false;
                currentPWM = 0;
            }
        } else {
            // Invalid payload length - ignore
            return;
        }
        applyPWM(true);
    });

    knxActive = true;
}

void shutdownKNX() {
    if (!knxActive) return;
    Serial.println("[KNX] Shutting down KNX stack...");
    mqtt_unsubscribe((knxTopicBase + "/+/0/on").c_str());
    knxActive = false;
}

bool isKNXActive() {
    return knxActive;
}

void handleKNX() {
    if (!knxActive) return;
    mqtt_handle();  // process incoming KNX messages via shared MQTT client
}

// Publish current light state to KNX (state report topic)
void publishKNXState() {
    if (!knxActive || knxTopicBase.isEmpty()) return;
    String onOff = ledOn ? "1" : "0";
    mqtt_publish((knxTopicBase + "/" + onOff + "/0/state").c_str(), String(currentPWM).c_str());
}
