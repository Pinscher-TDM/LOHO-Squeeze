#ifndef MQTT_HANDLER_H
#define MQTT_HANDLER_H

#include <PubSubClient.h>

void setupMQTT();
void reconnectMQTT();
void handleMQTT();  // process incoming messages via shared client
void publishMQTTState();
void publishHADiscovery();

// MQTT Broker Client Stack functions - runs MQTT without web server
void initMQTTBrokerClient();
void shutdownMQTTBrokerClient();
bool isMQTTBrokerClientActive();
void handleMQTTBrokerClient();

// Shared MQTT client API (used by homespan and KNX handlers)
void mqtt_publish(const char* topic, const char* payload);
void mqtt_subscribe(const char* topic, void (*callback)(char*, unsigned int));
void mqtt_unsubscribe(const char* topic);

#endif
