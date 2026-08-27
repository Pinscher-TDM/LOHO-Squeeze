#ifndef MQTT_HANDLER_H
#define MQTT_HANDLER_H

#include "connection_stack_manager.h"

// MQTT Broker Client Stack functions - memory-efficient, no HA discovery publishing
void initMQTTBrokerClient(const MQTTConfig& config);
void shutdownMQTTBrokerClient();
bool isMQTTBrokerClientActive();
void handleMQTTBrokerClient();

#endif