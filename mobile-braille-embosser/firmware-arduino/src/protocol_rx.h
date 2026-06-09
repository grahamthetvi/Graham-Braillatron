#pragma once

#include <stdint.h>

void protocol_rx_init(void);

/* Drains the serial input and dispatches any complete, CRC-valid frames. */
void protocol_rx_poll(uint32_t now_ms);
