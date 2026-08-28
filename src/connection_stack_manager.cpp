#include "connection_stack_manager.h"
#include "config.h"
#include "web_server.h"
#include "mqtt_handler.h"
#include "knx_handler.h"

ConnectionState ConnectionStackManager::connectionState = ConnectionState::NONE;

ConnectionState ConnectionStackManager::getConnectionState() {
    return connectionState;
}

void ConnectionStackManager::setConnectionState(ConnectionState state) {
    // Only allow transitions from NONE to an active state - switching
    // between active stacks requires a reboot.
    if (state != ConnectionState::NONE && connectionState == ConnectionState::NONE) {
        connectionState = state;
    } else if (connectionState != ConnectionState::NONE) {
        Serial.printf("[STACK] Connection already established as %s, rejecting switch\n",
                      getConnectionStateString(connectionState));
    }
}

bool ConnectionStackManager::isAnyStackActive() {
    return connectionState != ConnectionState::NONE;
}

const char* ConnectionStackManager::getConnectionStateString(ConnectionState state) {
    switch (state) {
        case ConnectionState::WEB_SERVER:
            return "Web Server";
        case ConnectionState::HOMESPAN:
            return "HomeSpan";
        case ConnectionState::KNX:
            return "KNX IP Interface";
        case ConnectionState::MQTT_ONLY:
            return "MQTT Broker Client Only";
        default:
            return "None";
    }
}

void ConnectionStackManager::startConfiguredStack() {
    if (isAnyStackActive() || settings.wifiRadioOff) return;

    ConnectionState state = ConnectionState::NONE;
    if (settings.knxEnabled && !settings.mqttEnabled) {
        state = ConnectionState::KNX;
    } else if (settings.homespanEnabled) {
        // HomeSpan uses MQTT, so require MQTT to be enabled
        if (!settings.mqttEnabled) return;  // invalid config - ignore
        state = ConnectionState::HOMESPAN;
    } else if (settings.mqttEnabled && !settings.knxEnabled) {
        // MQTT is enabled but KNX isn't - use plain broker client
        state = ConnectionState::MQTT_ONLY;
    } else {
        state = ConnectionState::WEB_SERVER;
    }
    if (state == ConnectionState::NONE) return;

    setConnectionState(state);
    Serial.printf("[STACK] Starting %s stack...\n", getConnectionStateString(state));

    if (state == ConnectionState::WEB_SERVER) {
        initWebServer();
    } else if (state == ConnectionState::HOMESPAN) {
        initHomeSpan();
    } else {
        initMQTTBrokerClient();
    }
}
