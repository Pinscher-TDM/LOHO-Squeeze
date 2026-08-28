#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "homespan_handler.h"
#include "knx_handler.h"

// HomeSpan initialization (called from web server when WiFi connects)
void initHomeSpan();

// Web Server Stack functions
void setupWiFi();
void initWebServer();
void shutdownWebServer();
bool isWebServerActive();
void handleWebServer();
void initWebServerRoutes();

#endif
