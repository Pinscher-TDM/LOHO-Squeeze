#include "discovery.h"
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <WiFi.h>

// BUG FIX: discovery was write-only and internally inconsistent.
//
//  1. initDiscovery() joined the multicast group 239.255.255.250, but
//     broadcastPresence() sent to 224.0.0.251 (the mDNS group). Nothing that
//     listened could ever hear what was sent.
//  2. The "local subnet broadcast" was sent to WiFi.localIP() - the device's
//     OWN unicast address - so it reached nobody but itself.
//  3. The payload was a JSON object with SSDP-looking keys ("M-SEARCH", "MAN",
//     "MX"). That is not SSDP; a real SSDP responder would ignore it. Since
//     only our own firmware needs to parse it, it may as well be honest JSON.
//  4. Nothing ever called parsePacket(), so inbound announcements were never
//     read and no peer list existed. handleDiscovery() was declared in
//     web_server.h but never defined.
//
// Send and receive now both use the SSDP group/port, and every packet carries
// a "loho" marker so unrelated SSDP traffic on port 1900 is ignored cheaply.

// PORT CHOICE: an earlier version of this used the SSDP group/port
// (239.255.255.250:1900). That is a busy address on any real network - routers,
// smart TVs, DLNA servers and every phone doing an M-SEARCH broadcast to it
// constantly. Binding there meant handleDiscovery() woke up for every one of
// those packets and ran a JSON parse against each, purely to reject it. In AP
// mode there is no such traffic, which is exactly why the device behaved in AP
// mode and struggled once it joined a real LAN.
//
// This is our own protocol with no third-party interop requirement, so it gets
// its own administratively-scoped group and port instead.
static const IPAddress DISCOVERY_GROUP(239, 255, 77, 77);
static const uint16_t  DISCOVERY_PORT = 50077;

// Hard cap on packets drained per loop() pass. Without this a burst of traffic
// could hold the loop long enough for WebServer client reads to time out.
static const int MAX_PACKETS_PER_PASS = 4;

static WiFiUDP  udp;
static bool     udpReady = false;

static LohoPeer peers[MAX_PEERS];
static size_t   peerCount = 0;

void initDiscovery() {
    // Defer until WiFi is up: joining a multicast group without a network
    // interface fails silently and leaves the socket unusable.
    if (WiFi.status() != WL_CONNECTED) return;
    udpReady = udp.beginMulticast(DISCOVERY_GROUP, DISCOVERY_PORT);
    if (!udpReady) {
        Serial.println("[DISC] Failed to join discovery multicast group");
    }
}

void broadcastPresence(const char* ssid, uint32_t lampId) {
    (void)ssid;  // kept for call-site compatibility; the SSID is not announced
    if (!udpReady || WiFi.status() != WL_CONNECTED) return;

    JsonDocument doc;
    doc["loho"] = 1;
    doc["id"]   = lampId;
    doc["name"] = deviceHostname();
    doc["ip"]   = WiFi.localIP().toString();

    char packet[192];
    size_t n = serializeJson(doc, packet, sizeof(packet));
    if (n == 0 || n >= sizeof(packet)) return;   // serialisation overflowed

    udp.beginPacket(DISCOVERY_GROUP, DISCOVERY_PORT);
    udp.write(reinterpret_cast<const uint8_t*>(packet), n);
    udp.endPacket();
}

// Insert or refresh a peer. Full table evicts the least recently heard entry,
// so a live lamp always displaces a stale one.
static void upsertPeer(uint32_t id, const char* name, const IPAddress& ip) {
    for (size_t i = 0; i < peerCount; i++) {
        if (peers[i].id == id) {
            peers[i].name     = name;
            peers[i].ip       = ip;
            peers[i].lastSeen = millis();
            return;
        }
    }

    size_t slot;
    if (peerCount < MAX_PEERS) {
        slot = peerCount++;
    } else {
        slot = 0;
        for (size_t i = 1; i < peerCount; i++) {
            if (peers[i].lastSeen < peers[slot].lastSeen) slot = i;
        }
    }
    peers[slot].id       = id;
    peers[slot].name     = name;
    peers[slot].ip       = ip;
    peers[slot].lastSeen = millis();
}

static void expirePeers() {
    unsigned long now = millis();
    size_t out = 0;
    for (size_t i = 0; i < peerCount; i++) {
        // Unsigned subtraction, so this stays correct across millis() rollover.
        if (now - peers[i].lastSeen < PEER_TTL_MS) {
            if (out != i) peers[out] = peers[i];
            out++;
        }
    }
    for (size_t i = out; i < peerCount; i++) peers[i] = LohoPeer();
    peerCount = out;
}

void handleDiscovery() {
    if (!udpReady || WiFi.status() != WL_CONNECTED) return;

    int len;
    int processed = 0;
    while (processed++ < MAX_PACKETS_PER_PASS && (len = udp.parsePacket()) > 0) {
        char buf[192];
        if (len >= (int)sizeof(buf)) {   // not one of ours - discard whole packet
            udp.flush();
            continue;
        }
        int n = udp.read(reinterpret_cast<uint8_t*>(buf), sizeof(buf) - 1);
        if (n <= 0) continue;
        buf[n] = '\0';

        // Cheap reject before paying for a JSON parse: our announce always
        // starts with '{'. On a dedicated port stray traffic should be rare,
        // but the group is still reachable by anything on the LAN.
        if (buf[0] != '{') continue;

        JsonDocument doc;
        if (deserializeJson(doc, buf)) continue;
        if (doc["loho"].isNull()) continue;

        uint32_t id = doc["id"] | 0u;
        if (id == 0) continue;
        if (id == getLampId()) continue;   // our own multicast loops back

        const char* name = doc["name"] | "";
        upsertPeer(id, name, udp.remoteIP());
    }

    expirePeers();
}

size_t getPeerCount() {
    return peerCount;
}

const LohoPeer* getPeer(size_t i) {
    if (i >= peerCount) return nullptr;
    return &peers[i];
}
