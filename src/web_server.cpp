#include "web_server.h"
#include "config.h"
#include "light_control.h"
#include "ota_updater.h"

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <vector>

extern std::vector<GitHubRelease> cachedReleases;

static WebServer server(80);
static DNSServer dnsServer;

static bool apMode = false;
static bool serverStarted = false;
static bool mdnsStarted = false;

static unsigned long lastWifiRetry = 0;

// -----------------------------------------------------------------------------
// Device identity
// -----------------------------------------------------------------------------

static String deviceHostname;
static String deviceMac;

// Generate a unique hostname from the ESP32 MAC address.
//
// Example:
//   MAC: AA:BB:CC:DD:A1:B2
//   hostname: esp32-a1b2
//
// Only the last two bytes are used in the hostname. This keeps the hostname
// short while still making devices on the same network effectively unique.
static void initializeDeviceIdentity()
{
deviceMac = WiFi.macAddress();


if (deviceMac.length() < 17) {
    deviceHostname = "LOHOsqueeze-device";
    Serial.println("[DEVICE] WARNING: Could not read MAC address");
    return;
}

// MAC format:
// AA:BB:CC:DD:EE:FF
//
// Take EE and FF.
String macByte1 = deviceMac.substring(12, 14);
String macByte2 = deviceMac.substring(15, 17);

macByte1.toLowerCase();
macByte2.toLowerCase();

deviceHostname = "Squeeze-" + macByte1 + macByte2;

Serial.printf("[DEVICE] MAC: %s\n", deviceMac.c_str());
Serial.printf("[DEVICE] Hostname: %s.local\n", deviceHostname.c_str());


}

static String getDeviceHostname()
{
if (deviceHostname.length() == 0) {
initializeDeviceIdentity();
}


return deviceHostname;


}

// -----------------------------------------------------------------------------
// CORS
// -----------------------------------------------------------------------------

// The main webapp may run from another hostname/origin.
// Allow browser JavaScript to communicate with the ESP32 API.
static void addCorsHeaders()
{
server.sendHeader("Access-Control-Allow-Origin", "*");
server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
server.sendHeader("Access-Control-Max-Age", "86400");
}

static void handleCorsOptions()
{
addCorsHeaders();


server.send(
    204,
    "text/plain",
    ""
);


}

// -----------------------------------------------------------------------------
// Wi-Fi events
// -----------------------------------------------------------------------------

static void onWiFiEvent(WiFiEvent_t event)
{
switch (event) {


    case ARDUINO_EVENT_WIFI_AP_START:
        Serial.println("[WiFi] AP started");
        break;

    case ARDUINO_EVENT_WIFI_AP_STOP:
        Serial.println("[WiFi] AP stopped");
        break;

    case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
        Serial.println("[WiFi] client connected to AP");
        break;

    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
        Serial.println("[WiFi] client left AP");
        break;

    case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED:
        Serial.println("[WiFi] client got IP");
        break;

    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
        Serial.println("[WiFi] STA connected");
        break;

    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
        Serial.println("[WiFi] STA disconnected");
        break;

    default:
        Serial.printf("[WiFi] event %d\n", (int)event);
        break;
}


}

// -----------------------------------------------------------------------------
// mDNS
// -----------------------------------------------------------------------------

static void startMDNS()
{
if (mdnsStarted) {
MDNS.end();
mdnsStarted = false;
}


String hostname = getDeviceHostname();

if (MDNS.begin(hostname.c_str())) {

    // Advertise HTTP service.
    MDNS.addService("http", "tcp", 80);

    mdnsStarted = true;

    Serial.printf(
        "[WEB] mDNS responder started: http://%s.local\n",
        hostname.c_str()
    );

} else {

    Serial.println(
        "[WEB] mDNS responder failed to start"
    );
}


}

// -----------------------------------------------------------------------------
// Wi-Fi setup
// -----------------------------------------------------------------------------

void setupWiFi()
{
WiFi.onEvent(onWiFiEvent);


// Make sure the device identity exists before configuring Wi-Fi.
initializeDeviceIdentity();

// The hostname reported to the DHCP server is the same hostname used
// by mDNS.
WiFi.setHostname(deviceHostname.c_str());

if (settings.ssid.length() > 0) {

    WiFi.mode(WIFI_STA);

    WiFi.setHostname(deviceHostname.c_str());

    WiFi.begin(
        settings.ssid.c_str(),
        settings.password.c_str()
    );

    // ESP32-C3 antenna workaround.
    WiFi.setTxPower(WIFI_POWER_8_5dBm);

    unsigned long start = millis();

    while (
        WiFi.status() != WL_CONNECTED &&
        millis() - start < 10000
    ) {
        delay(500);
    }
}

if (WiFi.status() == WL_CONNECTED) {

    apMode = false;

    Serial.printf(
        "[WEB] Wi-Fi connected, IP: %s\n",
        WiFi.localIP().toString().c_str()
    );

    Serial.printf(
        "[WEB] Device hostname: http://%s.local\n",
        deviceHostname.c_str()
    );

    startMDNS();

} else {

    // -----------------------------------------------------------------
    // Fallback AP
    // -----------------------------------------------------------------

    WiFi.mode(WIFI_AP);

    bool apOk = WiFi.softAP("LOHO-Squeeze");

    WiFi.setTxPower(WIFI_POWER_8_5dBm);

    Serial.printf(
        "[WEB] Fallback AP 'LOHO-Squeeze' %s, IP: %s\n",
        apOk ? "started" : "FAILED TO START",
        WiFi.softAPIP().toString().c_str()
    );

    Serial.printf(
        "[WEB] Device hostname: http://%s.local\n",
        deviceHostname.c_str()
    );

    apMode = true;

    dnsServer.start(
        53,
        "*",
        WiFi.softAPIP()
    );

    // Try to advertise mDNS on the AP interface as well.
    startMDNS();
}


}

// -----------------------------------------------------------------------------
// Background Wi-Fi reconnect
// -----------------------------------------------------------------------------

static void checkBackgroundReconnect()
{
if (WiFi.status() == WL_CONNECTED) {


    Serial.println(
        "[WEB] Wi-Fi connected in the background - "
        "shutting down fallback AP"
    );

    dnsServer.stop();

    WiFi.softAPdisconnect(true);

    WiFi.mode(WIFI_STA);

    WiFi.setHostname(deviceHostname.c_str());

    apMode = false;

    startMDNS();

    return;
}

if (settings.ssid.length() == 0)
    return;

unsigned long now = millis();

if (now - lastWifiRetry < WIFI_RETRY_INTERVAL_MS)
    return;

lastWifiRetry = now;

WiFi.mode(WIFI_AP_STA);

WiFi.setHostname(deviceHostname.c_str());

WiFi.begin(
    settings.ssid.c_str(),
    settings.password.c_str()
);

WiFi.setTxPower(WIFI_POWER_8_5dBm);


}

// -----------------------------------------------------------------------------
// Captive portal
// -----------------------------------------------------------------------------

static void sendCaptivePortalRedirect()
{
server.sendHeader(
"Location",
String("http://") +
WiFi.softAPIP().toString() +
"/",
true
);


server.sendHeader(
    "Connection",
    "close"
);

server.send(
    302,
    "text/plain",
    ""
);


}

// -----------------------------------------------------------------------------
// LittleFS
// -----------------------------------------------------------------------------

static String readFile(const char* path)
{
File f = LittleFS.open(path, "r");


if (!f)
    return "";

String contents = f.readString();

f.close();

return contents;


}

// -----------------------------------------------------------------------------
// Device information / pairing
// -----------------------------------------------------------------------------

static void handleApiInfo()
{
addCorsHeaders();


String mode = "unknown";

/*
 * apMode is the application's authoritative fallback-AP state.
 *
 * During the transition from AP+STA to STA we also check the
 * actual Wi-Fi state.
 */

if (
    WiFi.getMode() == WIFI_STA &&
    WiFi.status() == WL_CONNECTED
) {

    mode = "sta";

}
else if (
    WiFi.getMode() == WIFI_AP
) {

    mode = "ap";

}
else if (
    WiFi.getMode() == WIFI_AP_STA
) {

    /*
     * If STA has successfully connected, the device is considered
     * to be in normal Wi-Fi mode even if AP has not been disabled yet.
     */

    mode =
        (WiFi.status() == WL_CONNECTED)
            ? "sta"
            : "ap";

}
else if (apMode) {

    mode = "ap";
}

String hostname = getDeviceHostname();

String json = "{";

json += "\"id\":\"";
json += deviceMac;
json += "\",";

json += "\"hostname\":\"";
json += hostname;
json += "\",";

json += "\"address\":\"http://";
json += hostname;
json += ".local\",";

json += "\"mac\":\"";
json += deviceMac;
json += "\",";

json += "\"label\":\"LohoSqueeze\",";

json += "\"mode\":\"";
json += mode;
json += "\"";

json += "}";

server.sendHeader(
    "Connection",
    "close"
);

server.send(
    200,
    "application/json",
    json
);


}

// -----------------------------------------------------------------------------
// Web server routes
// -----------------------------------------------------------------------------

void initWebServerRoutes()
{
if (serverStarted)
return;


Serial.println(
    "[WEB] Initializing web server routes"
);

if (!LittleFS.begin(true)) {

    Serial.println(
        "[WEB] LittleFS Mount Failed"
    );

    return;
}

// ---------------------------------------------------------------------
// Pages
// ---------------------------------------------------------------------

server.on("/", HTTP_GET, []() {

    server.sendHeader(
        "Connection",
        "close"
    );

    server.send(
        200,
        "text/html",
        readFile("/index.html")
    );
});

server.on("/settings", HTTP_GET, []() {

    server.sendHeader(
        "Connection",
        "close"
    );

    server.send(
        200,
        "text/html",
        readFile("/settings.html")
    );
});

server.on("/update", HTTP_GET, []() {

    server.sendHeader(
        "Connection",
        "close"
    );

    server.send(
        200,
        "text/html",
        readFile("/update.html")
    );
});

server.on("/style.css", HTTP_GET, []() {

    server.sendHeader(
        "Connection",
        "close"
    );

    server.send(
        200,
        "text/css",
        readFile("/style.css")
    );
});

server.on("/loho-logo.svg", HTTP_GET, []() {

    String contents =
        readFile("/loho-logo.svg");

    if (contents.isEmpty()) {

        Serial.println(
            "[WEB] Logo file not found in LittleFS"
        );

        server.send(
            404,
            "text/plain",
            "Logo not found"
        );

        return;
    }

    server.sendHeader(
        "Content-Type",
        "image/svg+xml"
    );

    server.sendHeader(
        "Connection",
        "close"
    );

    server.send(
        200,
        "image/svg+xml",
        contents
    );
});

// ---------------------------------------------------------------------
// CORS preflight
// ---------------------------------------------------------------------

server.on(
    "/api/info",
    HTTP_OPTIONS,
    handleCorsOptions
);

server.on(
    "/api/state",
    HTTP_OPTIONS,
    handleCorsOptions
);

server.on(
    "/api/settings",
    HTTP_OPTIONS,
    handleCorsOptions
);

server.on(
    "/api/control",
    HTTP_OPTIONS,
    handleCorsOptions
);

server.on(
    "/api/releases",
    HTTP_OPTIONS,
    handleCorsOptions
);

server.on(
    "/api/ota",
    HTTP_OPTIONS,
    handleCorsOptions
);

server.on(
    "/save",
    HTTP_OPTIONS,
    handleCorsOptions
);

// ---------------------------------------------------------------------
// Device information / pairing
// ---------------------------------------------------------------------

server.on(
    "/api/info",
    HTTP_GET,
    handleApiInfo
);

// ---------------------------------------------------------------------
// State
// ---------------------------------------------------------------------

server.on("/api/state", HTTP_GET, []() {

    String json =
        "{\"ledOn\":" +
        String(
            ledOn
                ? "true"
                : "false"
        ) +

        ",\"btn\":" +
        String(
            isButtonPressed()
                ? "true"
                : "false"
        ) +

        ",\"pwm\":" +
        String(currentPWM) +

        ",\"minB\":" +
        String(settings.minBrightness) +

        ",\"maxB\":" +
        String(settings.maxBrightness) +

        "}";

    addCorsHeaders();

    server.sendHeader(
        "Connection",
        "close"
    );

    server.send(
        200,
        "application/json",
        json
    );
});

// ---------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------

server.on("/api/settings", HTTP_GET, []() {

    String json = "{";

    json += "\"ssid\":\"";
    json += settings.ssid;
    json += "\",";

    json += "\"minB\":";
    json += String(
        settings.minBrightness
    );
    json += ",";

    json += "\"maxB\":";
    json += String(
        settings.maxBrightness
    );
    json += ",";

    json += "\"speed\":";
    json += String(
        settings.dimSpeed
    );
    json += ",";

    json += "\"partyMode\":";
    json += String(
        settings.partyMode
            ? "true"
            : "false"
    );

    json += "}";

    addCorsHeaders();

    server.sendHeader(
        "Connection",
        "close"
    );

    server.send(
        200,
        "application/json",
        json
    );
});

// ---------------------------------------------------------------------
// Control
// ---------------------------------------------------------------------

server.on("/api/control", HTTP_GET, []() {

    if (server.hasArg("toggle")) {

        ledOn = !ledOn;
    }

    if (server.hasArg("pwm")) {

        currentPWM = constrain(
            server.arg("pwm").toInt(),
            settings.minBrightness,
            settings.maxBrightness
        );

        ledOn = true;
    }

    applyPWM(true);

    settings.lastPWM =
        currentPWM;

    saveSettings();

    addCorsHeaders();

    server.sendHeader(
        "Connection",
        "close"
    );

    server.send(
        200,
        "text/plain",
        "OK"
    );
});

// ---------------------------------------------------------------------
// Save settings
// ---------------------------------------------------------------------

server.on("/save", HTTP_POST, []() {

    if (server.hasArg("ssid")) {

        settings.ssid =
            server.arg("ssid");
    }

    // Only overwrite password when a new password was entered.
    if (
        server.hasArg("password") &&
        server.arg("password").length() > 0
    ) {

        settings.password =
            server.arg("password");
    }

    if (server.hasArg("minB")) {

        settings.minBrightness =
            server.arg("minB").toInt();
    }

    if (server.hasArg("maxB")) {

        settings.maxBrightness =
            server.arg("maxB").toInt();
    }

    if (server.hasArg("speed")) {

        settings.dimSpeed =
            server.arg("speed").toInt();
    }

    if (server.hasArg("partyMode")) {

        settings.partyMode =
            server.arg("partyMode") == "on";
    }

    saveSettings();

    addCorsHeaders();

    server.sendHeader(
        "Connection",
        "close"
    );

    server.send(
        200,
        "text/html",
        "Saved. Rebooting..."
    );

    delay(1000);

    ESP.restart();
});

// ---------------------------------------------------------------------
// GitHub Releases OTA
// ---------------------------------------------------------------------

server.on("/api/releases", HTTP_GET, []() {

    String err;

    if (
        !fetchGitHubReleases(
            cachedReleases,
            err
        )
    ) {

        addCorsHeaders();

        server.sendHeader(
            "Connection",
            "close"
        );

        server.send(
            502,
            "application/json",
            "{\"error\":\"" +
            err +
            "\"}"
        );

        return;
    }

    String json = "[";

    for (
        size_t i = 0;
        i < cachedReleases.size();
        i++
    ) {

        if (i)
            json += ",";

        const GitHubRelease& r =
            cachedReleases[i];

        json += "{";

        json += "\"index\":";
        json += String(i);
        json += ",";

        json += "\"tag\":\"";
        json += r.tag;
        json += "\",";

        json += "\"name\":\"";
        json += r.name;
        json += "\",";

        json += "\"date\":\"";
        json += r.publishedAt;
        json += "\",";

        json += "\"hasFirmware\":";
        json += String(
            r.assetUrl.length() > 0
                ? "true"
                : "false"
        );
        json += ",";

        json += "\"size\":";
        json += String(
            (unsigned long)
            r.assetSize
        );

        json += "}";
    }

    json += "]";

    addCorsHeaders();

    server.sendHeader(
        "Connection",
        "close"
    );

    server.send(
        200,
        "application/json",
        json
    );
});

// ---------------------------------------------------------------------
// OTA
// ---------------------------------------------------------------------

server.on("/api/ota", HTTP_POST, []() {

    if (!server.hasArg("index")) {

        addCorsHeaders();

        server.sendHeader(
            "Connection",
            "close"
        );

        server.send(
            400,
            "text/plain",
            "missing 'index' argument"
        );

        return;
    }

    int idx =
        server.arg("index").toInt();

    if (
        idx < 0 ||
        idx >= (int)cachedReleases.size()
    ) {

        addCorsHeaders();

        server.sendHeader(
            "Connection",
            "close"
        );

        server.send(
            400,
            "text/plain",
            "invalid release index - fetch /api/releases again"
        );

        return;
    }

    GitHubRelease release =
        cachedReleases[idx];

    Serial.printf(
        "[OTA] Update requested: %s\n",
        release.tag.c_str()
    );

    String err;

    bool ok =
        performOTAUpdate(
            release,
            err
        );

    addCorsHeaders();

    server.sendHeader(
        "Connection",
        "close"
    );

    if (ok) {

        server.send(
            200,
            "text/plain",
            "OK - rebooting into " +
            release.tag
        );

        delay(500);

        ESP.restart();

    } else {

        server.send(
            500,
            "text/plain",
            "Update failed: " +
            err
        );
    }
});

// ---------------------------------------------------------------------
// Captive portal probes
// ---------------------------------------------------------------------

server.on(
    "/generate_204",
    HTTP_GET,
    sendCaptivePortalRedirect
);

server.on(
    "/gen_204",
    HTTP_GET,
    sendCaptivePortalRedirect
);

server.on(
    "/hotspot-detect.html",
    HTTP_GET,
    sendCaptivePortalRedirect
);

server.on(
    "/library/test/success.html",
    HTTP_GET,
    sendCaptivePortalRedirect
);

server.on(
    "/ncsi.txt",
    HTTP_GET,
    sendCaptivePortalRedirect
);

server.on(
    "/connecttest.txt",
    HTTP_GET,
    sendCaptivePortalRedirect
);

server.on(
    "/canonical.html",
    HTTP_GET,
    sendCaptivePortalRedirect
);

// ---------------------------------------------------------------------
// 404
// ---------------------------------------------------------------------

server.onNotFound([]() {

    if (apMode) {

        sendCaptivePortalRedirect();

    } else {

        server.sendHeader(
            "Connection",
            "close"
        );

        server.send(
            404,
            "text/plain",
            "Not found"
        );
    }
});

server.begin();

serverStarted = true;

Serial.println(
    "[WEB] Web server started"
);


}

// -----------------------------------------------------------------------------
// Web Server lifecycle
// -----------------------------------------------------------------------------

void initWebServer()
{
if (serverStarted)
return;


setupWiFi();

initWebServerRoutes();


}

bool isWebServerActive()
{
return serverStarted;
}

void shutdownWebServer()
{
if (!serverStarted)
return;

Serial.println(
    "[WEB] Shutting down web server stack..."
);

server.stop();

dnsServer.stop();

if (mdnsStarted) {

    MDNS.end();

    mdnsStarted = false;
}

if (apMode) {

    WiFi.softAPdisconnect(true);

    apMode = false;
}

serverStarted = false;


}

void handleWebServer()
{
if (!serverStarted)
return;

server.handleClient();

if (apMode) {

    dnsServer.processNextRequest();

    checkBackgroundReconnect();
}


}
