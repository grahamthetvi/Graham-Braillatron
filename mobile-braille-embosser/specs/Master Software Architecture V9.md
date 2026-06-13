# Master Software Architecture & Engineering Specification V9

**Project:** Mobile Smart Braille Notetaker & Embosser (Graham Brailler)

**Lead Architect:** Addison

**Target Hardware:** Orange Pi 3B (SBC) + Custom PCB (HAT)

**Operating System:** DietPi Linux (Debian 13 Trixie, Vendor Kernel 6.1.115)

**Co-Processor:** Arduino Micro (ATmega32U4, 5V Logic)

*Canonical product and software specification. Hardware/PCB lifecycle detail and breadboard-to-manufacturing notes: [Master Architecture V4.9](Master%20Architecture%20V4.9.md). Wire protocol: `shared/protocol.h` and `shared/protocol.md`.*

---

## 1. Product Vision & ScreenReader Paradigm

The Graham Brailler is a portable electromechanical Smart Braille Notetaker and Embosser running headless DietPi. A single **ScreenReader UI Paradigm** (focus-based linear navigation) serves blind, low-vision, and deaf-blind users with identical underlying logic; the **Output Hub** routes focused content to the user's preferred channels.

### 1.1 Universal Inputs (Concurrent)

Users can utilize multiple input methods simultaneously without locking out others:

- **Built-in Braille Keyboard:** Standard 6-key Perkins layout with center D-pad (Up/Down) and action keys.
  - **Backspace:** Left of the D-pad.
  - **Enter:** Right of the D-pad.
  - **Shift / TTS:** Directly beneath Enter; hardware pause/resume for speech synthesis.
  - **Speech:** Push-to-talk for Vosk STT (menus, naming prompts, writing).
  - **Menu:** Invokes the global system overlay.
- **Peripheral QWERTY:** USB/Bluetooth; Windows/Super = Menu, Win+H = Dictation.
- **Refreshable Braille Displays:** USB/Bluetooth via BRLTTY (e.g. Mantis Q40).
- All inputs process concurrently without locking out others.

### 1.2 Universal Outputs (Distribution Hub)

Whenever focus changes or a word is announced, the Output Hub distributes content to parallel channels:

| Channel | Hardware / Software | Module |
|---------|---------------------|--------|
| TTS | eSpeak NG via Speech Dispatcher; MAX98357A I2S + 3.5 mm jack | `output_hub.cpp`, `backend.cpp` |
| Refreshable Braille | BRLTTY brlapi + liblouis forward translation | `backend.cpp`, `liblouis_bridge.cpp` |
| Visual Display | ST7789 SPI panel (240×240) + ncurses dev fallback; UI chrome | `ui/display/*`, `output_hub.cpp` |
| Embosser | Solenoid stagger head via `MotionService` | `motion_service.cpp`, `emboss_scheduler.cpp` |
| Haptics | DRV2605L LRA; Morse timed pulses | `drv2605l.cpp`, `morse_encoder.cpp` |

**Deaf-blind menu parity:** When TTS is disabled and `deaf_blind_menu_parity` is enabled, the Output Hub embosses full menu text (no abbreviations) in addition to refreshable braille/haptics.

### 1.3 Production Keyboard Driver (Direct Pin Topology)

The legacy 4×4 switch matrix, steering diodes, and external 10 kΩ pull-ups are **retired**. Production uses direct-pin wiring (Skeleton Prototype V5.1 Build Guide, Part 3.1): one side of each Cherry MX switch ties to a common ground bus; the other routes to a dedicated Arduino Micro input pin (`INPUT_PULLUP`, active LOW). One pin per key gives inherent N-key rollover with no ghosting and no diodes.

1. **Scanning:** The Arduino samples all 13 key pins once per millisecond from a non-blocking main loop (no `delay()`, no heavy ISR work that could starve the freefall interrupt).
2. **Debounce:** Per-key software integrator (15 ms threshold for Cherry MX): counter charges while pressed and discharges while released; debounced state flips only at the rails.
3. **Chord assembly:** On first debounced dot key-down, a 40 ms integration window opens; all dot presses within the window aggregate into one chord. Function keys bypass the window and transmit immediately on edge.

**The 13 keys:** 6 Braille dots, D-pad Up/Down, Backspace, Enter, Shift/TTS, Speech, Menu.

---

## 2. Application Architecture

Applications are categorized by session type and paper ownership.

### 2.1 Standalone Applications (Foreground)

Take primary control of embosser head and paper feed. Launched from the main app launcher or focus menu.

| Application | Description | Code Module |
|-------------|-------------|-------------|
| **Brailler** | `.brf` document editor; edit modes; worksheet auto-record | `apps/brailler_app.cpp` |
| **Calculator** | Nemeth math; char/silent/space-affirm audio modes | `apps/calculator_app.cpp` |
| **Transcriber** | Vosk STT → liblouis → emboss; buffer failsafe | `apps/transcriber_app.cpp` |
| **Library** | EPUB/DAISY reading, Gutendex public domain search | `library_app.cpp`, `library_store.cpp`, `library_backend.cpp` |
| **Morse Learning** | Morse alphabet lessons and quiz via haptics | `apps/morse_learn_app.cpp` |
| **Network & Devices** | Wi-Fi scan/connect via NetworkManager; BT list stub | `apps/network_app.cpp` |
| **LocalSend** | Local file transfer (scaffold) | `apps/localsend_app.cpp` |
| **Settings** | TTS rate/volume, braille grade, haptics, language | `output_hub.cpp` settings submenu |

Framework: `app_registry.cpp`, `app_session.h`, `ui_context.h`.

### 2.2 Inline Applications (System Menu Overlay)

Callable while a Standalone app is active. Do **not** advance paper.

| Inline App | Description | Code Module |
|------------|-------------|-------------|
| **Quick Status** | Battery, network, date, time | `apps/quick_status_inline.cpp` |
| **Morse Code Output** | Passive text→Morse haptics | `apps/morse_output_inline.cpp` |
| **Paper Navigation** | Jump Y line index on tractor feed | `apps/paper_nav_inline.cpp` |
| **Save & Exit** | Force flush coords + BRF; exit app | `apps/save_exit_inline.cpp` |

Keyboard routing: `menu open → overlay`; `standalone active → AppSession`; `idle → FocusNavigator`.

---

## 3. Digital/Physical Editing & Paper Logic

Tractor-fed paper cannot erase dots. Software maintains digital/physical sync.

### 3.1 Document Editing Modes (Brailler)

1. **Emboss:** Continuous printing of finalized document.
2. **Edit via Audio & Emboss:** TTS reads line-by-line; user full-cells (⠿, mask `0x3F`) over mistake; embosser advances to blank line; replacement chord syncs digital `.brf`.
3. **Emboss & Edit:** Full document embossed; menu paper navigation + same full-cell replace mechanic.

Implementation: `edit_session.cpp`, `brf_store.cpp`.

### 3.2 Coordinate Memory

Persist `{x_microsteps, y_line_index, active_app_id, brf_path}` to `/var/lib/braillatron/ram/coords.json`. Flushed atomically on Save & Exit and battery-critical shutdown.

Module: `coordinate_state.cpp`.

### 3.3 Boot Homing

On boot when `motion_enabled=true`: reverse feed until Y-home endstop (TCST2103); set Y=0; fast-forward to saved `y_line_index`.

Module: `homing_service.cpp` in `braillatron-sentinel`; status at `/run/braillatron/homing.status`.

### 3.4 App Switching (Forward Feed)

When switching Standalone apps: reverse to paper-edge sensor (TCRT5000), measure page, feed to fresh page. Module: `paper_separator.cpp`.

---

## 4. Co-Processor & Inter-Processor Protocol

Low-level, high-frequency physical I/O is offloaded to the Arduino Micro so OS scheduling jitter cannot affect real-time safety.

```
[13 Direct-Pin Keys]     [MPU6050 Accelerometer]
         │                          │
         ▼ (1 kHz polling)           ▼ (hardware interrupt, INT0)
┌─────────────────────────────────────────────────────────────┐
│                 ARDUINO MICRO CO-PROCESSOR                  │
│  - 15 ms integrator debounce                                │
│  - 40 ms temporal chord integration                         │
│  - Sub-10 ms freefall interlock (MPU6050 → IRLZ44N gate)    │
│  - AVR hardware WDT + host comms watchdog                   │
└──────────────────────────────┬──────────────────────────────┘
                               │
                               ▼ USB CDC serial @ 115200 bps (/dev/ttyACM0)
                     [Orange Pi 3B — braillatron-ui / daemons]
```

### 4.1 Scan, Debounce & Chord Assembly

The main loop samples all 13 pins at 1 kHz. ISRs are reserved for the MPU6050 freefall interlock so keyboard work cannot starve safety.

- **Scan:** Each key read from its dedicated pin (`INPUT_PULLUP`, active LOW). No row strobing or diode network.
- **Debounce:** Independent integrator per key; 15 ms threshold at 1 ms ticks.
- **Chords:** First debounced dot key-down starts a 40 ms window; additional dots aggregate into a raw dot bitmask (`dot_mask`, bits 0–5 = dots 1–6). When the window expires, the locked mask is sent as `BRAILLATRON_OP_CHORD`. The Pi translates the mask to characters (`chord_engine` on Pi; liblouis Grade 2 support stays host-side).
- **Function keys:** D-pad, Backspace, Enter, Shift/TTS, Speech, Menu send edge-triggered `BRAILLATRON_OP_KEYBOARD_MATRIX` frames immediately (no chord window).

### 4.2 Wire Protocol (Version 1)

Authoritative definitions: `shared/protocol.h`, `shared/protocol.md`.

**Physical layer:** UART 115200 bps (device configurable via `hardware.conf`), little-endian, CRC16-CCITT-FALSE over header + payload.

**Frame layout:** `[sync | version | opcode | sequence_id | payload_len | payload | crc16]`

- sync: `0xA5`
- version: `1`
- max payload: 32 bytes

| Opcode | Direction | Payload | Purpose |
|--------|-----------|---------|---------|
| `0x01` KEYBOARD_MATRIX | Arduino → Pi | 2-byte `key_state` | Edge-triggered function keys |
| `0x02` TELEMETRY | Pi → Arduino | 3-byte telemetry | Battery %, temperature, limit flags |
| `0x03` SAFETY | Bidirectional | 5-byte fault broadcast | Freefall, comms loss, battery critical, etc. |
| `0x04` HEARTBEAT | Pi → Arduino | none | Periodic liveness; disarms after boot grace |
| `0x05` ACK_NACK | Reserved | — | Future use |
| `0x06` CHORD | Arduino → Pi | 1-byte `dot_mask` | Assembled Braille chord |

**Telemetry limit flags** (Pi → Arduino relay): `BRAILLATRON_LIMIT_PAPER_EDGE` (TCRT5000), `BRAILLATRON_LIMIT_Y_HOME` (TCST2103), `BRAILLATRON_LIMIT_MOTION_BLOCKED`, `BRAILLATRON_LIMIT_BATTERY_CRITICAL`.

**Safety fault codes** include `BRAILLATRON_FAULT_FREEFALL`, `BRAILLATRON_FAULT_COMMS_LOSS`, `BRAILLATRON_FAULT_BATTERY_CRITICAL`, `BRAILLATRON_FAULT_WATCHDOG_TIMEOUT`. Severity levels range from INFO through LATCHED (requires explicit clear).

Invalid CRC frames are dropped. Pi sends `HEARTBEAT` on the configured interval when the serial device is open.

### 4.3 Two-Layer Watchdog

1. **AVR hardware WDT (500 ms):** A hung main loop resets the MCU. Stepper rail defaults off until firmware completes a clean boot.
2. **Host comms watchdog:** After the first Pi heartbeat, a gap longer than the comms timeout (3 s) cuts VMOT and latches `BRAILLATRON_FAULT_COMMS_LOSS` until heartbeats resume.

Implementation: `firmware-arduino/src/watchdog.cpp`, `fail_safes.cpp`.

---

## 5. Hardware, Power, Safety & Kinematics

*Board-level power topology and TMC2209 bus layout: [Master Architecture V4.9](Master%20Architecture%20V4.9.md). Prototype pin wiring: [Skeleton Prototype V5.1 Build Guide](Skeleton%20Prototype%20V5.1%20Build%20Guide.md).*

**Retired from earlier specs:** Raspberry Pi 3B, servo-driven 6-key embosser array, 18650 TBD battery, 4×4 keyboard matrix with steering diodes.

### 5.1 Power Distribution

```
[USB-C PD Input] → [IP2368 PD Charger] → [4S 30A BMS w/ Balancer] (14.8 V nominal)
         │
         ├─ (15 A motor fuse, 85 °C thermal fuse) ──► [IRLZ44N MOSFET] ──► [15 A star terminal]
         │                                              ▲ Arduino gate control
         │                                              └──► 8× TMC2209 VMOT + star ground
         │
         └─ (3–5 A logic fuse) ──► [TPS5430 5 V buck] ──┬──► Orange Pi 3B
                                                          └──► Arduino Micro
                                                                    │
Orange Pi I2S1 ──► [MAX98357A + 470 µF + 0.1 µF local filter] ──► 4 Ω 3 W speaker
```

- **Logic rail:** TPS5430 buck from battery to filtered 5 V for Orange Pi and Arduino.
- **Motor rail:** 14.8 V through IRLZ44N (TC4420 gate driver) to TMC2209 VMOT; Arduino controls the gate.
- **Audio isolation:** MAX98357A powered from 5 V with local 470 µF + 0.1 µF at VDD/GND to keep Class D switching noise off the logic bus.
- **Battery telemetry:** LTC2944 on system I2C tracks capacity, current, and voltage (see §6.3).
- **High-current routing:** Motor VMOT and returns use off-board dual-row terminal blocks (up to 15 A), not prototype-board traces.
- **Thermal fuse:** Non-resettable 85 °C fuse clamped to a unified aluminum heatsink spanning all eight stepper drivers.

### 5.2 Real-Time Hardware Interlock (MPU6050)

- **Sensor:** MPU6050 on Arduino hardware I2C (SDA/SCL); freefall thresholds configured in hardware registers (`FF_THR` / `FF_DUR`) at boot.
- **Interrupt:** MPU6050 INT → Arduino INT0 (Pin 3), active high.
- **Gate drive:** IRLZ44N in series on the 14.8 V stepper/solenoid rail; TC4420 drives the gate from a dedicated Arduino GPIO.

**Sub-10 ms isolation loop:**

1. Freefall detected → MPU6050 INT pin goes high immediately.
2. INT0 ISR runs (bypasses keyboard polling).
3. ISR pulls gate driver low, cutting VMOT in under 10 ms.
4. ISR transmits `BRAILLATRON_OP_SAFETY` with `BRAILLATRON_FAULT_FREEFALL` so the Pi pauses the motion queue and alerts the user.

### 5.3 Dual-Bus TMC2209 UART Daisy Chain

The RK3566 cannot expose eight independent driver UART lines. Eight TMC2209 drivers daisy-chain across two UART buses with 1N4148 steering diodes and MS1/MS2 address wiring:

```
Orange Pi UART4 ──┬──► TMC2209 Driver 1 (addr 0)
                  ├──► TMC2209 Driver 2 (addr 1)
                  ├──► TMC2209 Driver 3 (addr 2)
                  └──► TMC2209 Driver 4 (addr 3)

Orange Pi UART9 ──┬──► TMC2209 Driver 5 (addr 0)
                  ├──► TMC2209 Driver 6 (addr 1)
                  ├──► TMC2209 Driver 7 (addr 2)
                  └──► TMC2209 Driver 8 (addr 3)
```

Wiring detail: Skeleton Prototype V5.1 Build Guide.

### 5.4 Staggered Embossing Head

Standard cells: left column dots 1–3, right column dots 4–6. Physical layout:

- **Row A (top):** Solenoids for dots 1, 3, 5.
- **Row B (bottom):** Solenoids for dots 2, 4, 6.
- **Spatial offset:** 2.5 mm along the X-axis (carriage path).

The motion controller must not fire all solenoids simultaneously. Row A fires as solenoids cross the target column; Row B data is buffered and fired after a velocity-derived delay equal to the time to travel 2.5 mm. Module: `emboss_scheduler.cpp`.

### 5.5 Stepper Drivers, Homing & Paper Sensing

- **Drivers:** TMC2209 SilentStepper for X (carriage) and Y (paper feed) axes.
- **Y-axis homing:** TCST2103 optical slot sensor (transmissive photointerrupter) — reverse feed until triggered; set Y = 0.
- **Paper edge / page boundary:** TCRT5000 reflective IR sensor on the cardstock path for page alignment and app-switch feed logic.

### 5.6 Engineering Constraints & Mitigations

| Constraint | Risk | Mitigation |
|------------|------|------------|
| Heavy stepper EMI | Audio instability, SoC noise | Digital I2S audio (MAX98357A); local 470 µF + 0.1 µF on amp VDD/GND |
| RK3566 pin limits | Cannot wire 8 independent driver UARTs | Dual-bus single-wire UART daisy chain with diodes + MS1/MS2 addresses |
| Sudden power loss | eMMC/SD corruption | Read-only root + tmpfs volatile mounts; atomic writes to `/data`; `braillatron-sync.timer` |
| Drop during motion | Head/solenoid damage | MPU6050 hardware interrupt → sub-10 ms IRLZ44N cut + SAFETY broadcast |
| Driver thermal runaway | Fire / hardware damage | Unified heatsink + 85 °C thermal fuse on motor rail |
| Multi-key Braille chords | Ghost keys (legacy matrix) | **Direct-pin topology** — one GPIO per key, no matrix (§1.3) |

---

## 6. OS, Storage & Telemetry Policy

### 6.1 Read-Only Root & Persistent `/data`

- Root `/` read-only; volatile paths (`/tmp`, `/var/log`, `/var/tmp`) on tmpfs via `deploy/os/setup-overlay-ro.sh` (logs, ephemeral state).
- User documents and settings on `/data/braillatron/`.
- Atomic writes: RAM buffer → `.tmp` → `fsync` → `rename`.
- `braillatron-sync.timer` mirrors RAM layers to flash so sudden interlock power cuts do not corrupt the OS partition.

### 6.2 Appliance Console vs Dev SSH

Production images boot directly into Braillatron — no login prompt, no local shell. End users power on and interact through the ScreenReader (physical keyboard, TTS, refreshable braille, SPI display). Bootstrap applies this via `deploy/os/setup-appliance-mode.sh`:

- **`braillatron.target`** starts at multi-user boot (systemd, not a login session).
- **Local getty disabled** — an attached monitor does not show a login prompt.
- **Root `/` read-only** — volatile paths (`/tmp`, `/var/log`, `/var/tmp`) on tmpfs; remount helpers at `/usr/local/sbin/braillatron-remount-rw` and `braillatron-remount-ro`.
- **SSH enabled** — development and maintenance over the network only.

| Surface | Access | Writable paths |
| --- | --- | --- |
| Appliance (local) | Keyboard + Output Hub only | `/data/braillatron/` (documents, settings, credentials) |
| Dev (SSH) | Normal shell | `/data/braillatron/` always; `/etc/braillatron/` and system files after `braillatron-remount-rw` |

Skip appliance lockdown during factory bring-up: `BRAILLATRON_APPLIANCE=0 sudo bash deploy/bootstrap-dietpi.sh`.

### 6.3 Battery Policy

| Threshold | Action |
|-----------|--------|
| 20% | One-time audio + haptic warning per session (UI reads `/run/braillatron/telemetry.json`) |
| 5% | Block motion, flush RAM/coords/BRF, shutdown haptic, graceful power off; LTC2944 triggers `BRAILLATRON_LIMIT_BATTERY_CRITICAL` relay |

### 6.4 Crash Reporting (Optional)

Sentry / Memfault via `crash_reporter.cpp`. Disabled when DSN/keys empty. **Never** attach document text, SSIDs, or user paths — stack traces and hardware metrics only.

### 6.5 OTA / A/B Updates — NOT YET ADDRESSED

> **Footnote:** Over-the-air A/B dual-bank updates (RAUC or Mender) are **not yet addressed**. Current deployment uses DietPi read-only root + `/data` transactional storage (`deploy/os/setup-overlay-ro.sh`). Future work must define partition layout and choose RAUC or Mender before client implementation.

### 6.6 Dependencies

- **TTS:** eSpeak NG (Speech Dispatcher) — Piper excluded.
- **STT:** Vosk-API + PipeWire capture.
- **Braille:** liblouis (UEB G1/G2, Nemeth).
- **Embosser:** C++ kinematics daemon (`motion_controller`, `emboss_scheduler`).

---

## 7. Standardized Hardware Reference

| Subsystem | Standardized Part |
|-----------|-------------------|
| SBC | Orange Pi 3B (4 GB LPDDR4, RK3566, WiFi/BT) |
| Co-processor | Arduino Micro (ATmega32U4, 5 V, native USB) |
| PD input / charge | IP2368 USB-C PD charger |
| Battery | 4S LiPo (14.8 V) BMS with active balancing |
| Logic power | TPS5430 synchronous buck (5 V) |
| Safety interlock | IRLZ44N N-channel MOSFET + TC4420 gate driver |
| Battery gas gauge | LTC2944 (I2C coulomb counter) |
| Audio amp | MAX98357A I2S Class D mono (Rockchip I2S1 bypass) |
| Internal speaker | 4 Ω 3 W or 8 Ω 2 W enclosed capsule (foam-isolated) |
| Audio filter | 470 µF low-ESR electrolytic + 0.1 µF ceramic at amp VDD/GND |
| Haptic driver | DRV2605L I2C |
| Haptic actuator | LRA (linear resonant actuator) |
| Paper edge sensor | TCRT5000 reflective IR |
| Y-axis homing | TCST2103 optical slot (transmissive) |
| Stepper drivers | 8× TMC2209 (dual UART daisy chain, §5.3) |
| Freefall sensor | MPU6050 (Arduino I2C + INT0) |
| Keyboard switches | 13× Cherry MX (direct pin, §1.3) |

---

## Appendix A — Implementation Status Matrix

| Spec Item | Status | Module |
|-----------|--------|--------|
| ScreenReader focus nav | Implemented | `focus_nav.cpp` |
| Visual display (UI chrome) | Implemented | `ui/display/*`, `output_hub.cpp` |
| Menu overlay | Implemented | `menu_overlay.cpp` |
| Output Hub TTS/BRL/STT/Haptics | Implemented | `output_hub.cpp` |
| Embosser output channel | Implemented | `EmbosserBackend`, `motion_service.cpp` |
| Deaf-blind menu parity | Implemented | `OutputHub::emit` policy |
| App registry / Standalone-Inline | Implemented | `app_registry.cpp` |
| Brailler + edit FSM | Implemented | `brailler_app.cpp`, `edit_session.cpp` |
| Document dictation (PTT → BRF) | Implemented | `brailler_app.cpp`, Settings toggle |
| Coordinate memory | Implemented | `coordinate_state.cpp` |
| Boot homing | Implemented | `homing_service.cpp` |
| Calculator Nemeth | Implemented | `calculator_app.cpp` |
| Transcriber pipeline | Implemented | `transcriber_app.cpp`, Vosk backend |
| Morse learning / output | Implemented | `morse_encoder.cpp`, inline + standalone apps |
| Network Wi-Fi | Implemented | `network_app.cpp` |
| connectd sidecar | Implemented (needs device validation) | `connect/`, `braillatron-connectd.service` |
| YouTube audio app | Implemented (needs device validation) | `youtube_app.cpp`, `youtube_backend.cpp` |
| Signal messaging app | Implemented (needs device validation) | `messages_app.cpp`, `signal_backend.cpp` |
| Timer (inline) | Implemented | `timer_service.cpp`, `timer_inline.cpp` |
| Dictionary (offline) | Implemented | `dictionary_store.cpp`, `dictionary_app.cpp` |
| Spelling (offline) | Implemented | `spelling_list_store.cpp`, `spelling_app.cpp` |
| Contacts (offline) | Implemented | `contacts_store.cpp`, `contacts_app.cpp` |
| Local Music Player | Implemented | `music_backend.cpp`, `music_app.cpp`, shared `mpv_service.cpp` |
| Weather | Implemented | `weather_backend.cpp`, `weather_app.cpp`, Open-Meteo cache |
| Podcasts | Implemented | `rss_backend.cpp`, `podcasts_app.cpp`, OPML import, shared mpv |
| Internet Radio | Implemented | `radio_backend.cpp`, `radio_app.cpp`, ICY metadata, favorites |
| connectd async IPC + global poll | Implemented | `connect_job_queue.cpp`, `connect_client.cpp`, `ui_app.cpp` |
| Library / LocalSend | **Implemented** (EPUB/DAISY/Gutendex; BARD/Bookshare deferred) | `library_app.cpp`, `library_store.cpp`, `library_backend.cpp`, `localsend_app.cpp` |
| Gmail | Implemented (needs device validation) | `gmail_app.cpp`, `gmail_backend.cpp`, OAuth device flow, BRF export to `documents/gmail/` |
| Inter-processor protocol v1 | Implemented | `shared/protocol.h`, firmware + daemon parsers |
| Telemetry JSON bridge | Implemented | `telemetry_bridge.cpp` |
| 20% battery warning | Implemented | `telemetry_sentinel.cpp`, UI poll |
| Crash reporter | Implemented (optional build) | `crash_reporter.cpp` |
| OTA A/B | **Not addressed** | — |
| Piper TTS | **Excluded** | — |

**Connectivity follow-up:** See [Connectivity Follow-Up Checklist](Connectivity%20Follow-Up%20Checklist.md).
