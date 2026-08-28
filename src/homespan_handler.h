#ifndef HOMESPAN_HANDLER_H
#define HOMESPAN_HANDLER_H

#include "config.h"
#include <Arduino.h>

// HomeSpan uses MQTT for communication - it publishes state updates and
// subscribes to control topics. This handler mirrors the same pattern as
// mqtt_handler.cpp but with HomeSpan-specific topic prefixes.
void initHomeSpan();
void handleHomeSpan();
void publishHomeSpanState();
extern void applyPWM(bool publish);
extern void mqtt_handle();

#endif // HOMESPAN_HANDLER_H
