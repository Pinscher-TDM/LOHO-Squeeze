#include "web_server.h"
#include "config.h"
#include "light_control.h"
#include "mqtt_handler.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>

static WebServer server(80);
static DNSServer dnsServer;
static bool apMode = false;
static bool serverStarted = false;      // BUG FIX: guards against double-registering routes
static unsigned long lastWifiRetry = 0;

// Diagnostic: log Wi-Fi lifecycle events so AP drops / client joins are
// visible on the serial monitor with a reason.
static void onWiFiEvent(WiFiEvent_t event) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_AP_START:           Serial.println("[WiFi] AP started"); break;
        case ARDUINO_EVENT_WIFI_AP_STOP:            Serial.println("[WiFi] AP stopped"); break;
        case ARDUINO_EVENT_WIFI_AP_STACONNECTED:    Serial.println("[WiFi] client connected to AP"); break;
        case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED: Serial.println("[WiFi] client left AP"); break;
        case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED:   Serial.println("[WiFi] client got IP"); break;
        case ARDUINO_EVENT_WIFI_STA_CONNECTED:      Serial.println("[WiFi] STA connected"); break;
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:   Serial.println("[WiFi] STA disconnected"); break;
        default: Serial.printf("[WiFi] event %d\n", (int)event); break;
    }
}

// WiFi setup - shared by web server stack
void setupWiFi() {
    WiFi.onEvent(onWiFiEvent);
    if (settings.ssid.length() > 0) {
        WiFi.mode(WIFI_STA);
        WiFi.setHostname(DEVICE_HOSTNAME);
        WiFi.begin(settings.ssid.c_str(), settings.password.c_str());

        unsigned long start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) delay(500);
    }

    if (WiFi.status() == WL_CONNECTED) {
        apMode = false;
        Serial.printf("[WEB] Wi-Fi connected, IP: %s\n", WiFi.localIP().toString().c_str());
        MDNS.begin(DEVICE_HOSTNAME);
        initHomeSpan();
    } else {
        WiFi.mode(WIFI_AP);
        bool apOk = WiFi.softAP("LOHO-Squeeze");
        Serial.printf("[WEB] Fallback AP 'LOHO-Squeeze' %s, IP: %s\n",
                      apOk ? "started" : "FAILED TO START",
                      WiFi.softAPIP().toString().c_str());
        apMode = true;
        dnsServer.start(53, "*", WiFi.softAPIP());
    }
}

// BUG FIX (missing feature): this project never shut the fallback AP down
// once real Wi-Fi became reachable, so it would stay in AP mode forever
// once it fell back to it, even after the router came back up. This
// retries the saved credentials in the background every
// WIFI_RETRY_INTERVAL_MS and tears the AP down the moment it connects.
static void checkBackgroundReconnect() {
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("[WEB] Wi-Fi connected in the background - shutting down fallback AP");
        dnsServer.stop();
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_STA);
        apMode = false;
        MDNS.begin(DEVICE_HOSTNAME);
        return;
    }

    if (settings.ssid.length() == 0) return;

    unsigned long now = millis();
    if (now - lastWifiRetry < WIFI_RETRY_INTERVAL_MS) return;
    lastWifiRetry = now;

    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(settings.ssid.c_str(), settings.password.c_str());
}

// Web Server route registration - idempotent, safe to call multiple times
void initWebServerRoutes() {
    if (serverStarted) return;  // Already initialized
    
    // Only start web server routes if KNX is not enabled (single-stack architecture)
    if (settings.knxEnabled) return;
    
    Serial.println("[WEB] Initializing web server routes");

    if (!LittleFS.begin(true)) {
        Serial.println("[WEB] LittleFS Mount Failed");
        return;
    }

    server.on("/", HTTP_GET, [&]() {
        server.sendHeader("Connection", "close");
        server.send(200, "text/html", LittleFS.open("/index.html", "r").readString());
    });

    server.on("/settings", HTTP_GET, [&]() {
        server.sendHeader("Connection", "close");
        server.send(200, "text/html", LittleFS.open("/settings.html", "r").readString());
    });

    server.on("/style.css", HTTP_GET, [&]() {
        server.sendHeader("Connection", "close");
        server.send(200, "text/css", LittleFS.open("/style.css", "r").readString());
    });

    server.on("/api/state", HTTP_GET, [&]() {
        String json = "{\"ledOn\":" + String(ledOn ? "true" : "false") +
                      ",\"pwm\":" + String(currentPWM) +
                      ",\"minB\":" + String(settings.minBrightness) +
                      ",\"maxB\":" + String(settings.maxBrightness) + "}";
        server.sendHeader("Connection", "close");
        server.send(200, "application/json", json);
    });

    // BUG FIX (missing feature): settings.html had no way to pre-fill the
    // form with current values, so saving would blank out anything you
    // didn't retype (including your Wi-Fi SSID/password). This feeds the
    // form on page load. Passwords are deliberately left out of this
    // response - see the /save handler for how "leave blank to keep" works.
    server.on("/api/settings", HTTP_GET, [&]() {
        String json = "{";
        json += "\"ssid\":\"" + settings.ssid + "\",";
        json += "\"minB\":" + String(settings.minBrightness) + ",";
        json += "\"maxB\":" + String(settings.maxBrightness) + ",";
        json += "\"speed\":" + String(settings.dimSpeed) + ",";
        json += "\"homespanEnabled\":" + String(settings.homespanEnabled ? "true" : "false") + ",";
        if (settings.homespanDeviceId.length() > 0) {
            json += "\"homespanDeviceId\":\"" + settings.homespanDeviceId + "\",";
        }

        json += "\"mqttServer\":\"" + settings.mqttServer + "\",";
        json += "\"mqttPort\":" + String(settings.mqttPort) + ",";
        json += "\"mqttUser\":\"" + settings.mqttUser + "\",";
        json += "\"mqttTopic\":\"" + settings.mqttTopicBase + "\"";
        json += "}";
        server.sendHeader("Connection", "close");
        server.send(200, "application/json", json);
    });

    server.on("/api/control", HTTP_GET, [&]() {
        if (server.hasArg("toggle")) ledOn = !ledOn;
        if (server.hasArg("pwm")) {
            currentPWM = constrain(server.arg("pwm").toInt(), settings.minBrightness, settings.maxBrightness);
            ledOn = true;
        }
        applyPWM(true);
        saveSettings();
        server.sendHeader("Connection", "close");
        server.send(200, "text/plain", "OK");
    });

    server.on("/api/ha-discover", HTTP_GET, [&]() {
        // BUG FIX: this used to unconditionally report success even if
        // MQTT wasn't enabled/connected, so the button lied.
        if (!settings.mqttEnabled) {
            server.sendHeader("Connection", "close");
            server.send(400, "text/plain", "MQTT is not enabled - turn it on and save first");
            return;
        }
        publishHADiscovery();
        server.sendHeader("Connection", "close");
        server.send(200, "text/plain", "OK");
    });

    server.on("/save", HTTP_POST, [&]() {
        if (server.hasArg("ssid")) settings.ssid = server.arg("ssid");
        // BUG FIX: only overwrite the password if a new one was actually
        // typed - the settings page never sends the old one back down (for
        // security), so previously every save wiped it out.
        if (server.hasArg("password") && server.arg("password").length() > 0) {
            settings.password = server.arg("password");
        }
        if (server.hasArg("homespanEnabled")) settings.homespanEnabled = server.hasArg("homespanEnabled");
        if (server.hasArg("homespanDeviceId")) settings.homespanDeviceId = server.arg("homespanDeviceId");
        if (server.hasArg("minB")) settings.minBrightness = server.arg("minB").toInt();
        if (server.hasArg("maxB")) settings.maxBrightness = server.arg("maxB").toInt();
        if (server.hasArg("speed")) settings.dimSpeed = server.arg("speed").toInt();
        settings.mqttEnabled = server.hasArg("mqttEnabled");
        if (server.hasArg("mqttServer")) settings.mqttServer = server.arg("mqttServer");
        if (server.hasArg("mqttPort")) settings.mqttPort = server.arg("mqttPort").toInt();
        if (server.hasArg("mqttUser")) settings.mqttUser = server.arg("mqttUser");
        if (server.hasArg("mqttPass") && server.arg("mqttPass").length() > 0) {
            settings.mqttPass = server.arg("mqttPass");
        }
        if (server.hasArg("mqttTopic")) settings.mqttTopicBase = server.arg("mqttTopic");
        saveSettings();

        server.sendHeader("Connection", "close");
        server.send(200, "text/html", "Saved. Rebooting...");
        delay(1000);
        ESP.restart();
    });

    server.onNotFound([&]() {
        if (apMode) {
            server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
            server.sendHeader("Connection", "close");
            server.send(302, "text/plain", "");
        } else {
            server.sendHeader("Connection", "close");
            server.send(404, "text/plain", "Not found");
        }
    });

    server.begin();
    serverStarted = true;
}

// Web Server stack lifecycle - idempotent, safe to call again
void initWebServer() {
    if (serverStarted) return;
    setupWiFi();
    initWebServerRoutes();
}

bool isWebServerActive() {
    return serverStarted && !settings.mqttEnabled;  // MQTT_ONLY mode disables web server
}

void shutdownWebServer() {
    if (!serverStarted) return;
    Serial.println("[WEB] Shutting down web server stack...");
    server.stop();
    dnsServer.stop();
    if (apMode) {
        WiFi.softAPdisconnect(true);
        apMode = false;
    }
    serverStarted = false;
}

void handleWebServer() {
    if (!serverStarted) return;  // Not initialized yet

    server.handleClient();
    if (apMode) {
        dnsServer.processNextRequest();
        checkBackgroundReconnect();
    }
}
