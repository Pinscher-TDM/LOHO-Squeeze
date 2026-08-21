#include "discovery.h"
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <WiFi.h>

// Static UDP socket for SSDP multicast broadcasts (224.0.0.251)
static WiFiUDP udp;

void initDiscovery() {
    // Defer discovery until after WiFi is fully initialized.
    // This avoids using the UDP socket in AP mode or before network stack is ready.
    if (WiFi.status() != WL_CONNECTED) return;
    udp.beginMulticast(IPAddress(239, 255, 255, 250), 1900);
}

void broadcastPresence(const char* ssid, uint32_t lampId) {
    // Build SSDP M-SEARCH request to announce presence (compact buffer)
    JsonDocument doc;
    doc["M-SEARCH"] = "*";
    doc["MAN"] = "\"ssdp:discover\"";
    doc["MX"] = "SECONDS=1";
    doc["ST"] = "uuid:ESPLamp-" + String(lampId, HEX);

    // SSDP request is ~80 bytes; use 128-byte buffer with dynamic sizing
    char request[128];
    size_t n = serializeJson(doc, request);
    request[n] = '\0';

    // Send to SSDP multicast address (works on WiFi)
    udp.beginPacket(IPAddress(224, 0, 0, 251), 1900);
    udp.print(request);
    udp.endPacket();

    // Also send a simple UDP broadcast to local subnet for discovery
    char idBuf[32];
    snprintf(idBuf, sizeof(idBuf), "ESPLamp-%04X", lampId);
    udp.beginPacket(WiFi.localIP(), 1900);
    udp.print(idBuf);
    udp.endPacket();
}
