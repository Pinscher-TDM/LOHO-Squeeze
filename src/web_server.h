#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>

// Web Server Stack functions
void setupWiFi();
void initWebServer();
void shutdownWebServer();
bool isWebServerActive();
void handleWebServer();
void initWebServerRoutes();

#endif
