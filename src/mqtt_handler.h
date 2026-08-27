#ifndef MQTT_HANDLER_H
#define MQTT_HANDLER_H

void setupMQTT();
void reconnectMQTT();
void handleMQTT();
void publishMQTTState();
void publishHADiscovery();

// MQTT Broker Client Stack functions - runs MQTT without web server/Matter
void initMQTTBrokerClient();
void shutdownMQTTBrokerClient();
bool isMQTTBrokerClientActive();
void handleMQTTBrokerClient();

#endif
