#pragma once

#include <string>

// Unique per-lamp ID derived from efuse MAC address (last 6 bytes)
constexpr uint32_t getLampId() {
    // ESP32-C3 efuse: last 16 bits of user efuse = unique lamp ID
    return ESP.getEfuseMac() & 0xFFFF;
}

// Hostname format: "LOHO-Squeeze-XXXX" where XXXX is the hex lamp ID
std::string getHostname() {
    uint32_t id = getLampId();
    char buf[16];
    snprintf(buf, sizeof(buf), "LOHO-Squeeze-%04X", id);
    return std::string(buf);
}

// UDP presence broadcast (multicast 224.0.0.251/SSDP) to announce presence
void broadcastPresence(const char* ssid, uint32_t lampId);
