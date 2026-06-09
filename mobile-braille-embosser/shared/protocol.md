# Inter-Processor Protocol (SPI / UART)

Shared packet definitions for firmware-arduino ↔ daemon-dietpi.

## Status

Version 1 implemented in `shared/protocol.h` and `shared/protocol.c`.

## Physical layer

- UART baud: 115200 (configurable via `hardware.conf`)
- Little-endian wire format
- CRC16-CCITT-FALSE over header + payload

## Frame format

`[sync | version | opcode | sequence_id | payload_len | payload | crc16]`

- sync: `0xA5`
- version: `1`
- max payload: 32 bytes

## Message types

| Opcode | Direction | Payload | Notes |
|--------|-----------|---------|-------|
| `0x01` KEYBOARD_MATRIX | Arduino → Pi | 2-byte key_state | Edge-triggered; logical `BRAILLATRON_KEY_*` bits |
| `0x02` TELEMETRY | Pi → Arduino | 3-byte telemetry | Reserved for relay |
| `0x03` SAFETY | Bidirectional | 5-byte fault broadcast | |
| `0x04` HEARTBEAT | Pi → Arduino | none | Sent periodically when serial is connected |
| `0x05` ACK_NACK | Reserved | none | Future use |
| `0x06` CHORD | Arduino → Pi | 1-byte dot_mask | Braille chord assembled on-device (40 ms window) |

## Keyboard input split

The Arduino owns debounce (15 ms integrator, direct-pin V5.1 topology) and the
40 ms braille chord integration window. Assembled chords arrive as `CHORD`
frames; function keys (D-pad, Enter, Backspace, Shift/TTS, Speech, Menu)
arrive as edge-triggered `KEYBOARD_MATRIX` state frames. The Pi translates the
chord dot mask to characters (`chord_engine`).

## Pi-side heartbeat

`braillatron-ui` sends `BRAILLATRON_OP_HEARTBEAT` zero-payload frames on the configured interval when `/dev/ttyACM0` (or configured device) is open. If the device is missing, heartbeat transmission is skipped silently.

## Error handling

- Invalid CRC frames are dropped by the receiver parser.
- The Arduino watches for host heartbeats: after the first heartbeat is seen,
  a gap longer than the comms timeout cuts the stepper rail and emits
  `SAFETY` with `BRAILLATRON_FAULT_COMMS_LOSS`.
- The Arduino also runs the AVR hardware watchdog; a hung main loop resets
  the MCU (stepper rail defaults to off until re-enabled in setup).
