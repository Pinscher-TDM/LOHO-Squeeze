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
static bool fsReady = false;            // LittleFS mounted successfully?

// Serves a file out of LittleFS, reporting *why* it is missing rather than
// handing the browser a blank 200. Both failure modes are recoverable by the
// user, so say exactly what to do about them.
static void serveStaticFile(const char *path, const char *contentType) {
    if (!fsReady) {
        server.send(503, "text/plain",
                    "Filesystem not mounted. Flash it with: pio run -t uploadfs");
        return;
    }
    File f = LittleFS.open(path, "r");
    if (!f || f.isDirectory()) {
        server.send(404, "text/plain",
                    "File missing from LittleFS. Flash it with: pio run -t uploadfs");
        return;
    }
    server.streamFile(f, contentType);
    f.close();
}

void setupWiFi() {
    if (settings.ssid.length() > 0) {
        WiFi.mode(WIFI_STA);
        // The ESP32-C3 has a SINGLE radio shared by Wi-Fi and BLE. Matter
        // advertises over BLE continuously while the device is uncommissioned,
        // and coexistence arbitration hands a large share of airtime to BLE.
        // Arduino additionally defaults Wi-Fi to modem sleep (WIFI_PS_MIN_MODEM),
        // which compounds the loss: the station misses beacons, TCP reads blow
        // their deadline, and NetworkClient::available() tears the HTTP
        // connection down on EAGAIN without retrying (unlike write(), which
        // explicitly tolerates it). Keeping the receiver always-on is the
        // standard mitigation. This is a mains-powered lamp, so the extra
        // current draw does not matter.
        WiFi.setSleep(false);
        WiFi.setHostname(deviceHostname());
        WiFi.begin(settings.ssid.c_str(), settings.password.c_str());
        Serial.println("WiFi begin");

        unsigned long start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) delay(500);
    }

    if (WiFi.status() == WL_CONNECTED) {
        apMode = false;
        MDNS.begin(deviceHostname());
        // BUG FIX: setupMatter() used to run here, inside setupWiFi(), which
        // main.cpp calls *before* initWebServer(). That directly contradicted
        // main.cpp's own comment ("Defer Matter until web server routes are
        // registered") and meant the Matter stack was already running and
        // competing for the network stack while the HTTP server was still
        // being set up. main.cpp now starts Matter after the server is up.
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
        MDNS.begin(deviceHostname());
        // BUG FIX: initDiscovery() bails out unless WiFi is already connected,
        // and it only ran once during setup() - which in AP-fallback mode was
        // before any connection existed. Recovering the link therefore left
        // discovery permanently dead. Re-initialise it here, on the same path
        // that restores mDNS and Matter.
        initDiscovery();
        setupMatter();
        return;
    }

    if (settings.ssid.length() == 0) return;

    unsigned long now = millis();
    if (now - lastWifiRetry < WIFI_RETRY_INTERVAL_MS) return;
    lastWifiRetry = now;

    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleep(false);   // see setupWiFi(): Wi-Fi/BLE coexistence
    WiFi.begin(settings.ssid.c_str(), settings.password.c_str());
}

void initWebServer() {
    // BUG FIX: toggleWiFiRadio() can now call this again after the radio
    // is turned back on - without this guard it would re-register every
    // route a second time and call server.begin() twice.
    if (serverStarted) return;

    // BUG FIX: a LittleFS mount failure used to `return` here, which skipped
    // every server.on() registration AND server.begin() - so the whole web UI
    // silently vanished (no dashboard, no settings page, no way to fix the
    // Wi-Fi credentials) instead of just the file-backed pages. Now we record
    // the failure and start the server anyway: the JSON API is NVS-backed and
    // keeps working, and the static routes report the real problem.
    fsReady = LittleFS.begin(true);
    if (!fsReady) {
        Serial.println("[WEB] LittleFS mount failed - static pages unavailable.");
        Serial.println("[WEB] Upload the filesystem image with: pio run -t uploadfs");
    }

    server.on("/", HTTP_GET, [&]() {
        serveStaticFile("/index.html", "text/html");
    });

    server.on("/settings", HTTP_GET, [&]() {
        serveStaticFile("/settings.html", "text/html");
    });

    server.on("/style.css", HTTP_GET, [&]() {
        serveStaticFile("/style.css", "text/css");
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

    // List all LOHO-Squeeze devices on the network (for multi-lamp control).
    //
    // BUG FIX: this used createNestedObject("devices"), which emits a single
    // JSON *object* - but index.html does `devices.length` and `devices[0]`,
    // so `.length` was undefined, the render bailed out early, and the device
    // list silently stayed empty. It also only ever described this device,
    // because nothing collected peers. Now it emits a proper array of self +
    // everything handleDiscovery() has heard from.
    //
    // StaticJsonDocument is deprecated in ArduinoJson 7; JsonDocument sizes
    // itself, which also removes the 512-byte cap that would have truncated
    // the list once a few lamps showed up.
    server.on("/api/devices", HTTP_GET, [&]() {
        JsonDocument doc;
        uint32_t myId = getLampId();
        doc["myId"] = myId;

        JsonArray devices = doc["devices"].to<JsonArray>();

        JsonObject self = devices.add<JsonObject>();
        self["id"]   = myId;
        self["name"] = deviceHostname();
        self["ip"]   = WiFi.localIP().toString();
        self["self"] = true;

        for (size_t i = 0; i < getPeerCount(); i++) {
            const LohoPeer* p = getPeer(i);
            if (!p) continue;
            JsonObject d = devices.add<JsonObject>();
            d["id"]   = p->id;
            d["name"] = p->name;
            d["ip"]   = p->ip.toString();
            d["self"] = false;
        }

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
