#ifndef KNX_HANDLER_H
#define KNX_HANDLER_H

#include "connection_stack_manager.h"

// KNX IP Interface Stack functions - memory-efficient, no unnecessary allocations
void initKNX(const KNXConfig& config);
void shutdownKNX();
bool isKNXActive();
void handleKNX();

#endif // KNX_HANDLER_H
