# Inter-Processor Protocol (UART)

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
| `0x02` TELEMETRY | Pi → Arduino | 3-byte telemetry | Battery %, temp, limit flags (`braillatron-ui` relay) |
| `0x03` SAFETY | Bidirectional | 5-byte fault broadcast | |
| `0x04` HEARTBEAT | Pi → Arduino | none | Sent periodically when serial is connected |
| `0x05` ACK_NACK | Reserved | none | Future use |
| `0x06` CHORD | Arduino → Pi | 1-byte dot_mask | Braille chord assembled on-device (40 ms window) |

## Keyboard input split

The Arduino owns debounce (15 ms integrator, direct-pin V5.1 topology) and the
40 ms braille chord integration window. Assembled chords arrive as `CHORD`
frames; function keys (D-pad, Enter, Backspace, Shift/TTS, Speech)
arrive as edge-triggered `KEYBOARD_MATRIX` state frames. **Menu** is software-only
(backtick / overlay). The Pi translates the chord dot mask to characters (`chord_engine`).

## Pi-side TELEMETRY relay

`braillatron-ui` reads `/run/braillatron/telemetry.json` (written by `braillatron-sentinel` from LTC2944, limit sensors, and policy) and sends `BRAILLATRON_OP_TELEMETRY` frames to the Arduino on the heartbeat interval (default 500 ms–1 s).

Payload (`braillatron_telemetry_t`):

- `battery_percent` — 0–100; `BRAILLATRON_TELEMETRY_UNKNOWN` (255) when unread
- `temperature_c` — °C; `BRAILLATRON_TELEMETRY_UNKNOWN_S8` (127) when unread
- `limit_status` — OR of `BRAILLATRON_LIMIT_*` flags:

| Flag | Meaning |
|------|---------|
| `BRAILLATRON_LIMIT_PAPER_EDGE` (bit 0) | TCRT5000 paper-edge sensor active |
| `BRAILLATRON_LIMIT_Y_HOME` (bit 1) | TCST2103 Y-home endstop active |
| `BRAILLATRON_LIMIT_MOTION_BLOCKED` (bit 2) | SOC &lt; 5 %, safety fault, or policy block |
| `BRAILLATRON_LIMIT_BATTERY_CRITICAL` (bit 3) | LTC2944 shutdown band — Arduino may cut VMOT |

The Arduino uses `BRAILLATRON_LIMIT_BATTERY_CRITICAL` in firmware to reinforce the hardware interlock.

## Pi-side SAFETY handling

On `BRAILLATRON_OP_SAFETY` with severity ≥ `BRAILLATRON_SEVERITY_CRITICAL`, `keyboard_service` blocks **MotionGate** and announces via Output Hub.

For `BRAILLATRON_FAULT_FREEFALL`, the Pi also calls the registered Klipper emergency-stop handler (**M112** via Moonraker) when `klipper.conf` `enabled=true`. Hardware VMOT is already cut by the Arduino ISR; M112 stops Monster8 motion that may still be alive over USB.

Other fault codes include `BRAILLATRON_FAULT_COMMS_LOSS`, `BRAILLATRON_FAULT_BATTERY_CRITICAL`, and `BRAILLATRON_FAULT_WATCHDOG_TIMEOUT`.

## Pi-side heartbeat

`braillatron-ui` sends `BRAILLATRON_OP_HEARTBEAT` zero-payload frames on the configured interval when `/dev/ttyACM0` (or configured device) is open. If the device is missing, heartbeat transmission is skipped silently.

## Error handling

- Invalid CRC frames are dropped by the receiver parser.
- The Arduino watches for host heartbeats: after the first heartbeat is seen,
  a gap longer than the comms timeout cuts the stepper rail and emits
  `SAFETY` with `BRAILLATRON_FAULT_COMMS_LOSS`.
- The Arduino also runs the AVR hardware watchdog; a hung main loop resets
  the MCU (stepper rail defaults to off until re-enabled in setup).
