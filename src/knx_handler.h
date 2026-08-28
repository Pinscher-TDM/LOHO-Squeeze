#ifndef KNX_HANDLER_H
#define KNX_HANDLER_H

// KNX IP Interface Stack functions - communicates via MQTT to a KNX installation.
// Topics are built from settings.mqttTopicBase + "/knx/..." so users can point
// it to their existing KNX group addresses.
void initKNX();
void shutdownKNX();
bool isKNXActive();
void handleKNX();

extern void applyPWM(bool publish);
extern void mqtt_handle();

#endif // KNX_HANDLER_H
