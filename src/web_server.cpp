#include "web_server.h"
#include "config.h"
#include "light_control.h"
#include "mqtt_handler.h"
#include "matter_handler.h"
#include "discovery.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Preferences.h>

static WebServer server(80);
static DNSServer dnsServer;
static bool apMode = false;
static bool serverStarted = false;      // BUG FIX: guards against double-registering routes
static unsigned long lastWifiRetry = 0;

void setupWiFi() {
    if (settings.ssid.length() > 0) {
        WiFi.mode(WIFI_STA);
        WiFi.setHostname(DEVICE_HOSTNAME);
        WiFi.begin(settings.ssid.c_str(), settings.password.c_str());
        Serial.println("WiFi begin");

        unsigned long start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) delay(500);
    }

    if (WiFi.status() == WL_CONNECTED) {
        apMode = false;
        MDNS.begin(DEVICE_HOSTNAME);
        setupMatter();
        Serial.printf("[BOOT] Connected to Wi-Fi: %s, IP: %s\n", settings.ssid.c_str(), WiFi.localIP().toString().c_str());
    } else {
        WiFi.mode(WIFI_AP);
        WiFi.softAP("LOHO-Squeeze");
        apMode = true;
        dnsServer.start(53, "*", WiFi.softAPIP());
        Serial.printf("[BOOT] AP mode - IP: %s\n", WiFi.softAPIP().toString().c_str());
    }
}

// BUG FIX (missing feature): this project never shut the fallback AP down
// once real Wi-Fi became reachable, so it would stay in AP mode forever
// once it fell back to it, even after the router came back up. This
// retries the saved credentials in the background every
// WIFI_RETRY_INTERVAL_MS and tears the AP down the moment it connects.
static void checkBackgroundReconnect() {
    if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
        Serial.println("Wi-Fi connected in the background - shutting down fallback AP");
        dnsServer.stop();
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_STA);
        apMode = false;
        MDNS.begin(DEVICE_HOSTNAME);
        setupMatter();
        return;
    }

    if (settings.ssid.length() == 0) return;

    unsigned long now = millis();
    if (now - lastWifiRetry < WIFI_RETRY_INTERVAL_MS) return;
    lastWifiRetry = now;

    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(settings.ssid.c_str(), settings.password.c_str());
}

void initWebServer() {
    // BUG FIX: toggleWiFiRadio() can now call this again after the radio
    // is turned back on - without this guard it would re-register every
    // route a second time and call server.begin() twice.
    if (serverStarted) return;

    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS Mount Failed");
        return;
    }

    server.on("/", HTTP_GET, [&]() {
        server.send(200, "text/html", LittleFS.open("/index.html", "r").readString());
    });

    server.on("/settings", HTTP_GET, [&]() {
        server.send(200, "text/html", LittleFS.open("/settings.html", "r").readString());
    });

    server.on("/style.css", HTTP_GET, [&]() {
        server.send(200, "text/css", LittleFS.open("/style.css", "r").readString());
    });

    server.on("/api/state", HTTP_GET, [&]() {
        String json = "{\"ledOn\":" + String(ledOn ? "true" : "false") +
                      ",\"pwm\":" + String(currentPWM) +
                      ",\"minB\":" + String(settings.minBrightness) +
                      ",\"maxB\":" + String(settings.maxBrightness) + "}";
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
        json += "\"matterEnabled\":" + String(settings.matterEnabled ? "true" : "false") + ",";
        json += "\"mqttEnabled\":" + String(settings.mqttEnabled ? "true" : "false") + ",";
        json += "\"mqttServer\":\"" + settings.mqttServer + "\",";
        json += "\"mqttPort\":" + String(settings.mqttPort) + ",";
        json += "\"mqttUser\":\"" + settings.mqttUser + "\",";
        json += "\"mqttTopic\":\"" + settings.mqttTopicBase + "\"";
        json += "}";
        server.send(200, "application/json", json);
    });

    server.on("/api/control", HTTP_GET, [&]() {
        if (server.hasArg("toggle")) ledOn = !ledOn;
        if (server.hasArg("pwm")) {
            currentPWM = constrain(server.arg("pwm").toInt(), settings.minBrightness, settings.maxBrightness);
            ledOn = true;
        }
        applyPWM(true, true);
        saveSettings();
        server.send(200, "text/plain", "OK");
    });

    server.on("/api/ha-discover", HTTP_GET, [&]() {
        // BUG FIX: this used to unconditionally report success even if
        // MQTT wasn't enabled/connected, so the button lied.
        if (!settings.mqttEnabled) {
            server.send(400, "text/plain", "MQTT is not enabled - turn it on and save first");
            return;
        }
        publishHADiscovery();
        server.send(200, "text/plain", "OK");
    });


    server.on("/api/matter-info", HTTP_GET, [&]() {
        String json = "{";
        json += "\"started\":" + String(isMatterStarted() ? "true" : "false") + ",";
        json += "\"commissioned\":" + String(isMatterCommissioned() ? "true" : "false") + ",";
        json += "\"pairingCode\":\"" + getMatterPairingCode() + "\",";
        json += "\"qrUrl\":\"" + getMatterQRCodeUrl() + "\"";
        json += "}";
        server.send(200, "application/json", json);
    });

    server.on("/api/matter-pair", HTTP_GET, [&]() {
        if (!isMatterStarted()) {
            server.send(400, "text/plain", "Matter hasn't started - enable it, save, and make sure the device is connected to Wi-Fi");
            return;
        }
        openMatterCommissioningWindow();
        server.send(200, "text/plain", "Commissioning window opened - open your Home app now and scan the code below");
    });

    // List all devices on the network (for multi-lamp control)
    server.on("/api/devices", HTTP_GET, [&]() {
        StaticJsonDocument<512> doc;

        uint32_t myId = getLampId();
        String name = DEVICE_HOSTNAME;
        String ip = WiFi.localIP().toString();

        // Build device object - use nested add for multiple fields
        JsonObject dev = doc.createNestedObject("devices");
        dev["name"] = name;
        dev["id"] = myId;
        dev["ip"] = ip;

        doc["myId"] = myId;

        String json;
        serializeJson(doc, json);
        server.send(200, "application/json", json);
    });

    server.on("/save", HTTP_POST, [&]() {
        if (server.hasArg("ssid")) settings.ssid = server.arg("ssid");
        // BUG FIX: only overwrite the password if a new one was actually
        // typed - the settings page never sends the old one back down (for
        // security), so previously every save wiped it out.
        if (server.hasArg("password") && server.arg("password").length() > 0) {
            settings.password = server.arg("password");
        }
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
        settings.matterEnabled = server.hasArg("matterEnabled");
        saveSettings();

        server.send(200, "text/html", "Saved. Rebooting...");
        delay(1000);
        ESP.restart();
    });

    server.onNotFound([&]() {
        if (apMode) {
            server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
            server.send(302, "text/plain", "");
        } else {
            server.send(404, "text/plain", "Not found");
        }
    });

    server.begin();
    Serial.println("[WEB] Server started");
    serverStarted = true;
}

void handleWebServer() {
    server.handleClient();
    if (apMode) {
        dnsServer.processNextRequest();
        checkBackgroundReconnect();
    }
}
