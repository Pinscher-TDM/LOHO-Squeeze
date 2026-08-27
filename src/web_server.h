#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "connection_stack_manager.h"

// Web Server + Matter Stack functions
void setupWiFi();
void initWebServer(const WebServerConfig& config);
void shutdownWebServer();
bool isWebServerActive();
void handleWebServer();
void initWebServerRoutes();

#endif