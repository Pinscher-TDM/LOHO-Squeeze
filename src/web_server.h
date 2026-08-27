#ifndef WEB_SERVER_H
#define WEB_SERVER_H

// Web Server + Matter Stack functions
void setupWiFi();
void initWebServer();
void shutdownWebServer();
bool isWebServerActive();
void handleWebServer();
void initWebServerRoutes();

#endif
