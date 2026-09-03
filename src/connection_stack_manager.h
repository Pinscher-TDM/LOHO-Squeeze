#ifndef CONNECTION_STACK_MANAGER_H
#define CONNECTION_STACK_MANAGER_H

#include <Arduino.h>

// Connection stack types - only one runs at a time.
//
// This barebones build only implements WEB_SERVER. HomeSpan/KNX/MQTT-only
// support previously existed in this project and were removed to keep this
// build small - to bring one back:
//   1. Add its handler files (e.g. mqtt_handler.h/.cpp) back under src/.
//   2. Add its enum value below.
//   3. Add a branch for it in ConnectionStackManager::startConfiguredStack().
//   4. Add a `case ConnectionState::XXX: handleXxx(); break;` in main.cpp's loop().
//   5. Add its settings fields to AppSettings in config.h (+ load/save +
//      a toggle in data/settings.html) so it can be turned on from the UI.
enum class ConnectionState : uint8_t {
    NONE = 0,
    WEB_SERVER,
    // HOMESPAN,  // reserved for re-addition
    // KNX,       // reserved for re-addition
    // MQTT_ONLY, // reserved for re-addition
};

// Ensures only one stack runs at a time (matches the original single-radio,
// single-stack architecture).
class ConnectionStackManager {
public:
    static ConnectionState getConnectionState();
    static void setConnectionState(ConnectionState state);
    static bool isAnyStackActive();
    static const char* getConnectionStateString(ConnectionState state);

    // Picks a stack from the saved settings and starts it. No-op if a
    // stack is already active or the Wi-Fi radio is switched off.
    static void startConfiguredStack();

private:
    static ConnectionState connectionState;
};

#endif // CONNECTION_STACK_MANAGER_H
