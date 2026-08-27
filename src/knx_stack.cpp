#include "knx_handler.h"
#include <Arduino.h>

// KNX IP Interface Stack - placeholder until a KNX library is chosen and
// wired up. initKNX() deliberately refuses to mark the stack active so the
// rest of the firmware never routes traffic to it.

static bool knxActive = false;

void initKNX() {
    if (knxActive) return;
    Serial.println("[KNX] KNX IP interface stack is not implemented yet");
}

void shutdownKNX() {
    knxActive = false;
}

bool isKNXActive() {
    return knxActive;
}

void handleKNX() {
    if (!knxActive) return;
}
