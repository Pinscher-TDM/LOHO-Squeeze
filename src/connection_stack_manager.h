#ifndef CONNECTION_STACK_MANAGER_H
#define CONNECTION_STACK_MANAGER_H

#include <Arduino.h>

// Connection stack types - only one can be active at a time
enum class ConnectionState : uint8_t {
    NONE = 0,
    WEB_SERVER,   // Web server + Matter (shares WiFi)
    KNX,          // KNX IP interface
    MQTT_ONLY     // MQTT broker client only
};

// Stack configuration structures - memory-efficient defaults
struct WebServerConfig {
    bool matterEnabled = true;
    String ssid = "";
    String password = "";
    int minBrightness = 5;
    int maxBrightness = 255;
    int dimSpeed = 15;
};

struct KNXConfig {
    IPAddress knxIp{239, 255, 0, 1};
    uint16_t knxPort = 3671;
    String knxGroupAddress = "LOHO/";
};

struct MQTTConfig {
    bool brokerOnly = false;   // true = connect to broker as client only (no publishing)
    String server = "";
    int port = 1883;
    String user = "";
    String pass = "";
    String topicBase = "Squeeze/led";
};

// Forward declarations for stack implementations
extern void initWebServer(const WebServerConfig& config);
extern void shutdownWebServer();
extern bool isWebServerActive();
extern void handleWebServer();

extern void initKNX(const KNXConfig& config);
extern void shutdownKNX();
extern bool isKNXActive();
extern void handleKNX();

extern void initMQTTBrokerClient(const MQTTConfig& config);
extern void shutdownMQTTBrokerClient();
extern bool isMQTTBrokerClientActive();
extern void handleMQTTBrokerClient();

// Connection stack manager - ensures only one stack runs at a time
// Uses static variables for simplicity and memory efficiency
class ConnectionStackManager {
public:
    enum class StackType : uint8_t {
        NONE = 0,
        WEB_SERVER_STACK,   // Web server + Matter (shares WiFi)
        KNX_STACK,
        MQTT_BROKER_STACK
    };

    static ConnectionState getConnectionState();
    static void setConnectionState(ConnectionState state);
    static StackType getActiveStackType();
    static bool isAnyStackActive();
};

#endif // CONNECTION_STACK_MANAGER_H
