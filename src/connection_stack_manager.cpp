#include "connection_stack_manager.h"
#include <Arduino.h>

// Connection stack manager implementation - uses static variables for memory efficiency
ConnectionState ConnectionStackManager::getConnectionState() {
    return connectionState;
}

void ConnectionStackManager::setConnectionState(ConnectionState state) {
    // Only allow transitions to a different active state (not back to NONE)
    if (state != ConnectionState::NONE && connectionState == ConnectionState::NONE) {
        // Transitioning from NONE - safe to switch
        connectionState = state;
    } else if (connectionState != ConnectionState::NONE) {
        // Already active - reject transition
        Serial.printf("[STACK] Connection already established as %s, rejecting switch\n",
                      getConnectionStateString(connectionState));
    }
}

ConnectionStackManager::StackType ConnectionStackManager::getActiveStackType() {
    switch (connectionState) {
        case ConnectionState::WEB_SERVER:
            return StackType::WEB_SERVER_STACK;
        case ConnectionState::KNX:
            return StackType::KNX_STACK;
        case ConnectionState::MQTT_ONLY:
            return StackType::MQTT_BROKER_STACK;
        default:
            return StackType::NONE;
    }
}

bool ConnectionStackManager::isAnyStackActive() {
    return connectionState != ConnectionState::NONE;
}

const char* ConnectionStackManager::getConnectionStateString(ConnectionState state) {
    switch (state) {
        case ConnectionState::WEB_SERVER:
            return "Web Server + Matter";
        case ConnectionState::KNX:
            return "KNX IP Interface";
        case ConnectionState::MQTT_ONLY:
            return "MQTT Broker Client Only";
        default:
            return "None";
    }
}
