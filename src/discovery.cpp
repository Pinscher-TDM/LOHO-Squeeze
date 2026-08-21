#include "discovery.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>

// Static UDP socket for SSDP multicast broadcasts (224.0.0.251)
static WiFiUDP udp;

void broadcastPresence(const char* ssid, uint32_t lampId) {
    // Build SSDP M-SEARCH request to announce presence
    StaticJsonDocument<256> doc;
    doc["M-SEARCH"] = "*";
    doc["MAN"] = "\"ssdp:discover\"";
    doc["MX"] = "SECONDS=1";
    doc["ST"] = "uuid:ESPLamp-" + String(lampId, HEX);

    char request[256];
    size_t n = serializeJson(doc, request);
    request[n] = '\0';

    // Send to SSDP multicast address (works on WiFi)
    udp.beginMulticast(239.255.255.250, 1900);
    udp.beginPacket(IPAddress(224, 0, 0, 251), 1900);
    udp.print(request);
    udp.endPacket();

    // Also send a simple UDP broadcast to local subnet for discovery
    udp.beginPacket(WiFi.localIP(), 1900);
    udp.print("ESPLamp-" + String(lampId, HEX) + "\n");
    udp.endPacket();
}
