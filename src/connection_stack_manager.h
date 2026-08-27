#ifndef CONNECTION_STACK_MANAGER_H
#define CONNECTION_STACK_MANAGER_H

#include <Arduino.h>

// Connection stack types - only one can be active at a time:
//   WEB_SERVER: Web server + Matter (shares WiFi)
//   KNX:        KNX IP interface
//   MQTT_ONLY:  MQTT broker client only
enum class ConnectionState : uint8_t {
    NONE = 0,
    WEB_SERVER,
    KNX,
    MQTT_ONLY
};

// Connection stack manager - ensures only one stack runs at a time.
class ConnectionStackManager {
public:
    static ConnectionState getConnectionState();
    static void setConnectionState(ConnectionState state);
    static bool isAnyStackActive();
    static const char* getConnectionStateString(ConnectionState state);

    // Picks a stack from the saved settings and starts it. No-op if a
    // stack is already active, the Wi-Fi radio is switched off, or the
    // settings don't select exactly one stack.
    static void startConfiguredStack();

private:
    static ConnectionState connectionState;
};

#endif // CONNECTION_STACK_MANAGER_H
