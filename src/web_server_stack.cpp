#include "web_server.h"
#include "connection_stack_manager.h"

// Web Server + Matter Stack Implementation
// This stack shares WiFi with Matter, so they run together as one logical unit

void initWebServer(const WebServerConfig& config) {
    // Only initialize if not already active (idempotent)
    if (ConnectionStackManager::isWebServerActive()) return;

    settings = config;  // Store configuration for later use

    // Initialize LittleFS with truncation to ensure clean state
    if (!LittleFS.begin(true)) {
        Serial.println("[WEB] LittleFS Mount Failed");
        return;
    }

    // Setup WiFi (STA or AP fallback)
    setupWiFi();

    // Setup Matter - runs alongside web server sharing the same WiFi connection
    setupMatter();

    // Initialize Web Server routes
    initWebServerRoutes();
}

void shutdownWebServer() {
    if (!ConnectionStackManager::isWebServerActive()) return;

    Serial.println("[WEB] Shutting down web server and Matter stack...");

    // Stop Matter first (releases WiFi resources)
    Matter.end();

    // Stop Web Server
    server.stop();
    dnsServer.stop();

    // Disconnect WiFi if in AP mode
    if (apMode) {
        WiFi.softAPdisconnect(true);
        MDNS.end();
    }

    Serial.println("[WEB] Stack shutdown complete");
}

bool ConnectionStackManager::isWebServerActive() {
    return serverStarted;
}

void handleWebServer() {
    if (!ConnectionStackManager::isWebServerActive()) return;

    // Handle Web Server client connections
    server.handleClient();

    // Process DNS requests for AP mode
    if (apMode) {
        dnsServer.processNextRequest();
    }

    // Background WiFi reconnection check
    checkBackgroundReconnect();
}
