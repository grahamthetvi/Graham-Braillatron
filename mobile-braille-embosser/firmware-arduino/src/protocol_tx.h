#pragma once

#include <stdint.h>

void protocol_tx_init(void);
void protocol_tx_keyboard_matrix(uint16_t key_state);
void protocol_tx_chord(uint8_t dot_mask);
void protocol_tx_safety(uint8_t fault_code, uint8_t severity, uint16_t detail);
