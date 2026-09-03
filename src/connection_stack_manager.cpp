#include "connection_stack_manager.h"
#include "config.h"
#include "web_server.h"

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
        default:
            return "None";
    }
}

void ConnectionStackManager::startConfiguredStack() {
    if (isAnyStackActive() || settings.wifiRadioOff) return;

    // TODO(stacks): once other stacks are reintroduced, pick between them
    // here based on settings (see original project's version of this file
    // for the pattern: e.g. settings.mqttEnabled, settings.knxEnabled, ...).
    // For now there's only one option.
    ConnectionState state = ConnectionState::WEB_SERVER;

    setConnectionState(state);
    Serial.printf("[STACK] Starting %s stack...\n", getConnectionStateString(state));
    initWebServer();
}
