#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include "config.h"

// Unique per-lamp ID derived from the efuse MAC (low 16 bits).
static inline uint32_t getLampId() {
    return ESP.getEfuseMac() & 0xFFFF;
}

// Hostname format: "LOHO-Squeeze-XXXX" where XXXX is the hex lamp ID.
// Delegates to deviceHostname() so there is exactly one implementation - the
// previous copy here used a char[16] buffer and truncated the ID.
static inline String getHostname() {
    return String(deviceHostname());
}

// A LOHO-Squeeze seen announcing itself on the local network.
struct LohoPeer {
    uint32_t      id = 0;
    String        name;
    IPAddress     ip;
    unsigned long lastSeen = 0;
};

// Peers are dropped if they miss this long (3 announce intervals + slack).
static const unsigned long PEER_TTL_MS = 200000UL;
static const size_t        MAX_PEERS   = 8;

void initDiscovery();

// Drains inbound announce packets and ages out silent peers.
// MUST be called from loop() - without it no peer is ever discovered.
void handleDiscovery();

void broadcastPresence(const char* ssid, uint32_t lampId);

size_t           getPeerCount();
const LohoPeer*  getPeer(size_t i);
