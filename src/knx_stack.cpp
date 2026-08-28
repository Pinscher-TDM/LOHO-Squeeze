#include "knx_handler.h"
#include <Arduino.h>

// KNX IP Interface Stack - placeholder until a KNX library is chosen and
// wired up. initKNX() deliberately refuses to mark the stack active so the
// rest of the firmware never routes traffic to it.

extern void initKNX();
extern void shutdownKNX();
extern bool isKNXActive();
extern void handleKNX();
