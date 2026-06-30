#pragma once

#include "protocol.h"

void telemetry_handler_init(void);
void telemetry_handler_apply(const braillatron_telemetry_t *payload);
