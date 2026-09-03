#include "web_server.h"
#include "config.h"
#include "light_control.h"   // isButtonPressed() for /api/state
#include "ota_updater.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <vector>

static WebServer server(80);
static DNSServer dnsServer;
static bool apMode = false;
static bool serverStarted = false;      // guards against double-registering routes
static unsigned long lastWifiRetry = 0;

// Populated by GET /api/releases, consumed by index into POST /api/ota -
// keeps the release URLs (which are long) out of the client<->device
// round trip.
static std::vector<GitHubRelease> cachedReleases;

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
        // Many ESP32-C3 boards have a poorly matched PCB antenna that
        // distorts the signal at full TX power. Cap it (must run after
        // the radio starts).
        WiFi.setTxPower(WIFI_POWER_8_5dBm);

        unsigned long start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) delay(500);
    }

    if (WiFi.status() == WL_CONNECTED) {
        apMode = false;
        Serial.printf("[WEB] Wi-Fi connected, IP: %s\n", WiFi.localIP().toString().c_str());
        MDNS.begin(DEVICE_HOSTNAME);
    } else {
        WiFi.mode(WIFI_AP);
        bool apOk = WiFi.softAP("LOHO-Squeeze");
        WiFi.setTxPower(WIFI_POWER_8_5dBm);  // see comment above - C3 antenna fix
        Serial.printf("[WEB] Fallback AP 'LOHO-Squeeze' %s, IP: %s\n",
                      apOk ? "started" : "FAILED TO START",
                      WiFi.softAPIP().toString().c_str());
        apMode = true;
        dnsServer.start(53, "*", WiFi.softAPIP());
    }
}

// Once real Wi-Fi becomes reachable, retire the fallback AP. Retries the
// saved credentials in the background every WIFI_RETRY_INTERVAL_MS.
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
    WiFi.setTxPower(WIFI_POWER_8_5dBm);  // see comment above - C3 antenna fix
}

static void sendCaptivePortalRedirect() {
    server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
    server.sendHeader("Connection", "close");
    server.send(302, "text/plain", "");
}

static String readFile(const char* path) {
    File f = LittleFS.open(path, "r");
    if (!f) return "";
    String contents = f.readString();
    f.close();
    return contents;
}

// Web Server route registration - idempotent, safe to call multiple times
void initWebServerRoutes() {
    if (serverStarted) return;  // Already initialized

    Serial.println("[WEB] Initializing web server routes");

    if (!LittleFS.begin(true)) {
        Serial.println("[WEB] LittleFS Mount Failed");
        return;
    }

    server.on("/", HTTP_GET, []() {
        server.sendHeader("Connection", "close");
        server.send(200, "text/html", readFile("/index.html"));
    });

    server.on("/settings", HTTP_GET, []() {
        server.sendHeader("Connection", "close");
        server.send(200, "text/html", readFile("/settings.html"));
    });

    server.on("/update", HTTP_GET, []() {
        server.sendHeader("Connection", "close");
        server.send(200, "text/html", readFile("/update.html"));
    });

    server.on("/style.css", HTTP_GET, []() {
        server.sendHeader("Connection", "close");
        server.send(200, "text/css", readFile("/style.css"));
    });

    server.on("/api/state", HTTP_GET, []() {
        String json = "{\"ledOn\":" + String(ledOn ? "true" : "false") +
                      ",\"btn\":" + String(isButtonPressed() ? "true" : "false") +
                      ",\"pwm\":" + String(currentPWM) +
                      ",\"minB\":" + String(settings.minBrightness) +
                      ",\"maxB\":" + String(settings.maxBrightness) + "}";
        server.sendHeader("Connection", "close");
        server.send(200, "application/json", json);
    });

    // Pre-fills the settings form on load. Passwords are deliberately left
    // out of this response - see /save for how "leave blank to keep" works.
    server.on("/api/settings", HTTP_GET, []() {
        String json = "{";
        json += "\"ssid\":\"" + settings.ssid + "\",";
        json += "\"minB\":" + String(settings.minBrightness) + ",";
        json += "\"maxB\":" + String(settings.maxBrightness) + ",";
        json += "\"speed\":" + String(settings.dimSpeed);
        json += "}";
        server.sendHeader("Connection", "close");
        server.send(200, "application/json", json);
    });

    server.on("/api/control", HTTP_GET, []() {
        if (server.hasArg("toggle")) ledOn = !ledOn;
        if (server.hasArg("pwm")) {
            currentPWM = constrain(server.arg("pwm").toInt(), settings.minBrightness, settings.maxBrightness);
            ledOn = true;
        }
        applyPWM(true);
        settings.lastPWM = currentPWM;
        saveSettings();
        server.sendHeader("Connection", "close");
        server.send(200, "text/plain", "OK");
    });

    server.on("/save", HTTP_POST, []() {
        if (server.hasArg("ssid")) settings.ssid = server.arg("ssid");
        // Only overwrite the password if a new one was actually typed - the
        // settings page never sends the old one back down (for security).
        if (server.hasArg("password") && server.arg("password").length() > 0) {
            settings.password = server.arg("password");
        }
        if (server.hasArg("minB")) settings.minBrightness = server.arg("minB").toInt();
        if (server.hasArg("maxB")) settings.maxBrightness = server.arg("maxB").toInt();
        if (server.hasArg("speed")) settings.dimSpeed = server.arg("speed").toInt();
        saveSettings();

        server.sendHeader("Connection", "close");
        server.send(200, "text/html", "Saved. Rebooting...");
        delay(1000);
        ESP.restart();
    });

    // --- GitHub Releases OTA -------------------------------------------

    server.on("/api/releases", HTTP_GET, []() {
        String err;
        if (!fetchGitHubReleases(cachedReleases, err)) {
            server.sendHeader("Connection", "close");
            server.send(502, "application/json", "{\"error\":\"" + err + "\"}");
            return;
        }

        String json = "[";
        for (size_t i = 0; i < cachedReleases.size(); i++) {
            if (i) json += ",";
            const GitHubRelease& r = cachedReleases[i];
            json += "{";
            json += "\"index\":" + String(i) + ",";
            json += "\"tag\":\"" + r.tag + "\",";
            json += "\"name\":\"" + r.name + "\",";
            json += "\"date\":\"" + r.publishedAt + "\",";
            json += "\"hasFirmware\":" + String(r.assetUrl.length() > 0 ? "true" : "false") + ",";
            json += "\"size\":" + String((unsigned long)r.assetSize);
            json += "}";
        }
        json += "]";
        server.sendHeader("Connection", "close");
        server.send(200, "application/json", json);
    });

    // Blocking on purpose: this is a barebones build and the download +
    // flash only takes a handful of seconds. The HTTP response is only
    // sent once the outcome (success/failure) is known.
    server.on("/api/ota", HTTP_POST, []() {
        if (!server.hasArg("index")) {
            server.sendHeader("Connection", "close");
            server.send(400, "text/plain", "missing 'index' argument");
            return;
        }
        int idx = server.arg("index").toInt();
        if (idx < 0 || idx >= (int)cachedReleases.size()) {
            server.sendHeader("Connection", "close");
            server.send(400, "text/plain", "invalid release index - fetch /api/releases again");
            return;
        }

        GitHubRelease release = cachedReleases[idx]; // copy - server_ member could get reused
        Serial.printf("[OTA] Update requested: %s\n", release.tag.c_str());

        String err;
        bool ok = performOTAUpdate(release, err);
        server.sendHeader("Connection", "close");
        if (ok) {
            server.send(200, "text/plain", "OK - rebooting into " + release.tag);
            delay(500);
            ESP.restart();
        } else {
            server.send(500, "text/plain", "Update failed: " + err);
        }
    });

     // --- Captive portal probes -----------------------------------------
    server.on("/generate_204", HTTP_GET, sendCaptivePortalRedirect);            // Android
    server.on("/gen_204", HTTP_GET, sendCaptivePortalRedirect);                 // Android (older)
    server.on("/hotspot-detect.html", HTTP_GET, sendCaptivePortalRedirect);     // iOS / macOS
    server.on("/library/test/success.html", HTTP_GET, sendCaptivePortalRedirect); // iOS (older)
    server.on("/ncsi.txt", HTTP_GET, sendCaptivePortalRedirect);                // Windows
    server.on("/connecttest.txt", HTTP_GET, sendCaptivePortalRedirect);         // Windows 10+
    server.on("/canonical.html", HTTP_GET, sendCaptivePortalRedirect);          // Firefox / some Linux

    server.onNotFound([]() {
        if (apMode) {
            sendCaptivePortalRedirect();
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
    return serverStarted;
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
