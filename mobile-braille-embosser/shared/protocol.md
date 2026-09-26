# Inter-Processor Protocol (USB CDC)

Shared packet definitions for firmware-arduino ↔ daemon-dietpi.

## Status

Version 1 implemented in `shared/protocol.h` and `shared/protocol.c`.

## Physical layer

- USB CDC only (`Serial` on the Arduino Micro, typically `/dev/ttyACM0` on the Pi). Not USART1 on D0/D1.
- Baud: 115200 (configurable via `hardware.conf`; CDC still uses this `Serial.begin` rate)
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
| `0x03` SAFETY | Arduino → Pi | 5-byte fault broadcast | Pi never sends SAFETY |
| `0x04` HEARTBEAT | Pi → Arduino | none | Sent periodically when serial is connected |
| `0x05` ACK_NACK | Unused in v1 | none | Neither side emits |
| `0x06` CHORD | Arduino → Pi | 1-byte dot_mask | Braille chord assembled on-device (40 ms window) |
| `0x07` CLEAR_FAULT | Pi → Arduino | none | Drop latched FREEFALL and COMMS_LOSS, then re-apply D12. MPU-missing, battery-critical, and an INT that is still low keep the rail off |

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

For any SAFETY frame at severity ≥ critical (freefall, comms loss, sensor failure), the Pi calls the registered Klipper emergency-stop handler (Moonraker `emergency_stop`, with `M112` only if that call fails) when `klipper.conf` `enabled=true`. Hardware VMOT is already cut by the Arduino (freefall: D12 port write in the ISR, no serial in the ISR). The main loop re-asserts the hold and transmits `BRAILLATRON_OP_SAFETY`. Emergency stop halts Monster8 motion that is still alive over USB.

Recover is explicit: Settings / Factory Test call `hooks::recover_motion_gate()`, which sends `BRAILLATRON_OP_CLEAR_FAULT` (`0x07`, zero payload) before unblocking MotionGate. There is no acknowledgement. A successful write is not proof that D12 came back on. After a cut, re-home Y and jog the carriage before embossing ([Hardware Bring-Up To-Do](../specs/Hardware%20Bring-Up%20To-Do.md)).

Arduino-emitted SAFETY codes are `FREEFALL`, `COMMS_LOSS`, and `SENSOR_FAILURE`. Battery-critical is a TELEMETRY `LIMIT_BATTERY_CRITICAL` flag, not a Pi- or Arduino-sent SAFETY frame. `FAULT_WATCHDOG_TIMEOUT` and `FAULT_THERMAL` are unused/reserved (kept for parsing); a host heartbeat gap uses `COMMS_LOSS`, and there is no Arduino thermal sensor.

## Pi-side heartbeat

`braillatron-ui` sends `BRAILLATRON_OP_HEARTBEAT` zero-payload frames on the configured interval when `/dev/ttyACM0` (or configured device) is open. If the device is missing, heartbeat transmission is skipped silently.

## Error handling

- Invalid CRC frames are dropped by the receiver parser.
- The Arduino watches for host heartbeats. The rail stays off until the first
  one. After that, a gap longer than the comms timeout cuts the stepper rail,
  latches `BRAILLATRON_FAULT_COMMS_LOSS`, and keeps the rail off until
  `CLEAR_FAULT`. A resumed heartbeat does not clear the latch.
- The Arduino also runs the AVR hardware watchdog; a hung main loop resets
  the MCU. Setup drives D12 low and leaves it low until the next clean
  heartbeat. That reset does not emit `FAULT_WATCHDOG_TIMEOUT` (unused/reserved).
