#pragma once

#include "protocol.h"

#include <stdbool.h>

void telemetry_handler_init(void);
void telemetry_handler_apply(const braillatron_telemetry_t *payload);

/* True while BRAILLATRON_LIMIT_BATTERY_CRITICAL is holding D12/VMOT down. */
bool telemetry_handler_battery_critical(void);
