# Master Software Architecture & Engineering Specification V9

**Project:** Mobile Smart Braille Notetaker & Embosser (Graham Brailler)

**Lead Architect:** Addison

**Target Hardware:** Orange Pi 3B (SBC) + Custom PCB (HAT)

**Operating System:** DietPi Linux (Debian 13 Trixie, Vendor Kernel 6.1.115)

**Co-Processor:** Arduino Micro (ATmega32U4, 5V Logic)

*Canonical **software** specification (apps, protocol, OS, implementation status). Hardware/PCB lifecycle notes: [Master Architecture V4.9](Master%20Architecture%20V4.9.md). Wire protocol: `shared/protocol.h` and `shared/protocol.md`.*

**Hardware interconnect is not this document.** Order parts and wire against:

| Domain | Source of truth |
|--------|-----------------|
| Power / keys / MPU / VMOT gate | [Skeleton Prototype V5.1 Build Guide](Skeleton%20Prototype%20V5.1%20Build%20Guide.md) + `firmware-arduino/src/pins.h` — 12 physical keys, MPU INT **active-low latched FALLING on D7**, IP2368 **in parallel** on the WAGO bus, D12 high-side enable. Open bench work: [Hardware Bring-Up To-Do](Hardware%20Bring-Up%20To-Do.md) |
| Monster8 pins / endstops / currents | `klipper/printer.cfg` **only** |

Interconnect language below that still disagrees with those three files is stale. Solenoid heads, MPU **INT0**, and MPU **active-high** INT are retired.

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
  - **Menu:** Software overlay (backtick on a USB keyboard). Not a physical key.
- **Peripheral QWERTY:** USB/Bluetooth; Windows/Super = Menu, Win+H = Dictation.
- **Refreshable Braille Displays:** USB/Bluetooth via BRLTTY (e.g. Mantis Q40).
- All inputs process concurrently without locking out others.

### 1.2 Universal Outputs (Distribution Hub)

Whenever focus changes or a word is announced, the Output Hub distributes content to parallel channels:

| Channel | Hardware / Software | Module |
|---------|---------------------|--------|
| TTS | eSpeak NG via Speech Dispatcher over **ALSA** (3.5 mm aux default; **BlueALSA** for Bluetooth; I2S MAX98357A when selected). Not PipeWire. | `output_hub.cpp`, `backend.cpp` |
| Refreshable Braille | BRLTTY brlapi + liblouis forward translation | `backend.cpp`, `liblouis_bridge.cpp` |
| Visual Display | ST7789 SPI panel (240×240) + wireless remote browser viewer + ncurses dev fallback. Shipped `display.conf` sets `hdmi_enabled=true`. | `ui/display/*`, `display/*`, `output_hub.cpp` |
| Embosser | Six NEMA14 punch steppers (Monster8 slots 2–7) via `MotionService` — **not solenoids** | `motion_service.cpp`, `emboss_scheduler.cpp` |
| Haptics | DRV2605L LRA; Morse timed pulses | `drv2605l.cpp`, `morse_encoder.cpp` |

**Deaf-blind menu parity:** When TTS is disabled and `deaf_blind_menu_parity` is enabled, the Output Hub embosses full menu text (no abbreviations) in addition to refreshable braille/haptics.

### 1.3 Production Keyboard Driver (Direct Pin Topology)

The legacy 4×4 switch matrix, steering diodes, and external 10 kΩ pull-ups are **retired**. Production uses direct-pin wiring (Skeleton Prototype V5.1 Build Guide, Part 3.1): one side of each Cherry MX switch ties to a common ground bus; the other routes to a dedicated Arduino Micro input pin (`INPUT_PULLUP`, active LOW). One pin per key gives inherent N-key rollover with no ghosting and no diodes.

1. **Scanning:** The Arduino samples all **12 physical key** pins once per millisecond from a non-blocking main loop (no `delay()`, no heavy ISR work that could starve the freefall interrupt).
2. **Debounce:** Per-key software integrator (15 ms threshold for Cherry MX): counter charges while pressed and discharges while released; debounced state flips only at the rails.
3. **Chord assembly:** On first debounced dot key-down, a 40 ms integration window opens; all dot presses within the window aggregate into one chord. Function keys bypass the window and transmit immediately on edge.

**The 12 physical keys:** 6 Braille dots, D-pad Up/Down, Backspace, Enter, Shift/TTS, Speech. **Menu** is software-only (`BRAILLATRON_KEY_MENU` in protocol; backtick on bench keyboards).

---

## 2. Application Architecture

Applications are categorized by session type and paper ownership.

### 2.1 Standalone Applications (Foreground)

Take primary control of embosser head and paper feed. Launched from the main app launcher or focus menu.

| Application | Description | Code Module |
|-------------|-------------|-------------|
| **Brailler (Document)** | `.brf` editor; full-cell replace; optional PTT dictation. The three named edit modes are not selected by the UI | `apps/brailler_app.cpp` |
| **Calculator** | Nemeth math; char/silent/space-affirm audio modes | `apps/calculator_app.cpp` |
| **Transcriber** | Vosk STT → liblouis → emboss; buffer failsafe | `apps/transcriber_app.cpp` |
| **Dictionary** | Offline SQLite lookup; prefix search; TTS/braille read | `dictionary_store.cpp`, `apps/dictionary_app.cpp` |
| **Spelling** | Bundled + imported word lists; Learn / Quiz / Review modes | `spelling_list_store.cpp`, `apps/spelling_app.cpp` |
| **Contacts** | Offline address book; CSV/vCard import; emboss card | `contacts_store.cpp`, `apps/contacts_app.cpp` |
| **Library** | Local EPUB/DAISY reading; Gutendex public-domain search/download | `library_store.cpp`, `library_backend.cpp`, `apps/library_app.cpp` |
| **LocalSend** | Receive-only sidecar. The unit is installed and not pulled in by `braillatron.target` until enabled | `apps/localsend_app.cpp` |
| **Wikipedia** | Live English Wikipedia API. Not an offline reader | `apps/wikipedia_app.cpp` |
| **YouTube** | Search and audio playback via connectd + shared mpv | `apps/youtube_app.cpp`, `youtube_backend.cpp` |
| **Messages** | Signal chat list, thread read, compose/reply | `apps/messages_app.cpp`, `signal_backend.cpp` |
| **Music** | Local library scan/play; resume state; shared mpv | `apps/music_app.cpp`, `music_backend.cpp` |
| **Weather** | Open-Meteo fetch/cache; current/hourly/daily views | `apps/weather_app.cpp`, `weather_backend.cpp` |
| **Podcasts** | RSS/OPML subscriptions; episode download + mpv playback | `apps/podcasts_app.cpp`, `rss_backend.cpp` |
| **Radio** | Internet radio streams; favorites; ICY metadata | `apps/radio_app.cpp`, `radio_backend.cpp` |
| **Gmail** | OAuth device flow; inbox/read/compose/reply; BRF export. IMAP-linked school accounts can read; SMTP send is not wired | `apps/gmail_app.cpp`, `gmail_backend.cpp` |
| **Morse Learning** | Morse alphabet lessons and quiz via haptics | `apps/morse_learn_app.cpp` |
| **Network & Devices** | Wi-Fi scan/connect via wpa_supplicant (`wpa_cli`) | `apps/network_app.cpp` |
| **Bluetooth Setup** | Bluetooth scan; pair by name or MAC via `bluetoothctl` | `apps/bluetooth_setup_app.cpp` |
| **Settings** | TTS rate/volume, braille grade, dictation toggle, accounts | `output_hub.cpp` settings submenu |
| **Factory Test** | PIN-gated assembly diagnostics: motors, speaker, sensors, haptics, MotionGate | `apps/factory_test_app.cpp` |

Framework: `app_registry.cpp`, `app_session.h`, `ui_context.h`.

### 2.2 Inline Applications (System Menu Overlay)

Callable while a Standalone app is active. Do **not** advance paper.

| Inline App | Description | Code Module |
|------------|-------------|-------------|
| **Quick Status** | Battery, Wi-Fi, date/time, weather cache, connect daemon reachability; active timer if running | `apps/quick_status_inline.cpp` |
| **Timer** | Countdown, stopwatch, Pomodoro setup; alerts via Output Hub while any app is active | `timer_service.cpp`, `apps/timer_inline.cpp` |
| **Morse Code Output** | Passive text→Morse haptics | `apps/morse_output_inline.cpp` |
| **Paper Navigation** | Jump Y line index on tractor feed | `apps/paper_nav_inline.cpp` |
| **Save & Exit** | Force flush coords + BRF; exit app | `apps/save_exit_inline.cpp` |
| **Look up word** | Document overlay stub (v1.2); directs user to Dictionary app | `app_registry.cpp` overlay item when Document active |

Keyboard routing: `menu open → overlay`; `inline active → inline AppSession` (input); `standalone active → AppSession`; `idle → FocusNavigator`. `TimerService` ticks in `UiApp::poll()` regardless of foreground app.

---

## 3. Digital/Physical Editing & Paper Logic

Tractor-fed paper cannot erase dots. Software maintains digital/physical sync.

### 3.1 Document Editing Modes (Brailler)

1. **Emboss:** Continuous printing of finalized document.
2. **Edit via Audio & Emboss:** TTS reads line-by-line; user full-cells (⠿, mask `0x3F`) over mistake; embosser advances to blank line; replacement chord syncs digital `.brf`.
3. **Emboss & Edit:** Full document embossed; menu paper navigation + same full-cell replace mechanic.

What runs today is typing, save, and full-cell replace. `EditMode` is stored by `set_mode()` and the UI never selects the three modes above, so a session stays on the emboss path. Implementation: `edit_session.cpp`, `brf_store.cpp`.

### 3.2 Coordinate Memory

Persist `{x_microsteps, y_line_index, active_app_id, brf_path}` to `/var/lib/braillatron/ram/coords.json`. Flushed atomically on Save & Exit and battery-critical shutdown.

Module: `coordinate_state.cpp`.

### 3.3 Boot Homing

On boot when `motion_enabled=true`: reverse feed until Y-home endstop (TCST2103); set Y=0; fast-forward to saved `y_line_index`.

Module: `homing_service.cpp` in `braillatron-sentinel`; status at `/run/braillatron/homing.status`.

### 3.4 App Switching (Forward Feed)

When switching Standalone apps: feed to a **fresh page (33 lines)** on 100 lb cardstock (0.5 in perf spacing). `paper_separator.cpp` uses that fixed line count. It does not keep the distance measured on the way back to the paper edge. See [Hardware Bring-Up To-Do](Hardware%20Bring-Up%20To-Do.md).

---

## 4. Co-Processor & Inter-Processor Protocol

Low-level, high-frequency physical I/O is offloaded to the Arduino Micro so OS scheduling jitter cannot affect real-time safety.

```
[12 Direct-Pin Keys]     [MPU6050 Accelerometer]
         │                          │
         ▼ (1 kHz polling)           ▼ (hardware interrupt, INT6 / D7)
┌─────────────────────────────────────────────────────────────┐
│                 ARDUINO MICRO CO-PROCESSOR                  │
│  - 15 ms integrator debounce                                │
│  - 40 ms temporal chord integration                         │
│  - Freefall interlock (MPU6050 → D12 high-side enable)     │
│  - AVR hardware WDT + host comms watchdog                   │
└──────────────────────────────┬──────────────────────────────┘
                               │
                               ▼ USB CDC serial @ 115200 bps (/dev/ttyACM0)
                     [Orange Pi 3B — braillatron-ui / daemons]
```

### 4.1 Scan, Debounce & Chord Assembly

The main loop samples all **12 physical key** pins at 1 kHz. ISRs are reserved for the MPU6050 freefall interlock so keyboard work cannot starve safety. Menu is invoked via software overlay (backtick), not a physical GPIO.

- **Scan:** Each key read from its dedicated pin (`INPUT_PULLUP`, active LOW). No row strobing or diode network.
- **Debounce:** Independent integrator per key; 15 ms threshold at 1 ms ticks.
- **Chords:** First debounced dot key-down starts a 40 ms window; additional dots aggregate into a raw dot bitmask (`dot_mask`, bits 0–5 = dots 1–6). When the window expires, the locked mask is sent as `BRAILLATRON_OP_CHORD`. The Pi translates the mask to characters (`chord_engine` on Pi; liblouis Grade 2 support stays host-side).
- **Function keys:** D-pad, Backspace, Enter, Shift/TTS, Speech send edge-triggered `BRAILLATRON_OP_KEYBOARD_MATRIX` frames immediately (no chord window). **Menu** is software-only (backtick / overlay), not a physical GPIO.

### 4.2 Wire Protocol (Version 1)

Authoritative definitions: `shared/protocol.h`, `shared/protocol.md`.

**Physical layer:** USB CDC at 115200 (`Serial` on the Micro, typically `/dev/ttyACM0` on the Pi; device set in `hardware.conf`). Not USART1 on D0/D1. Little-endian, CRC16-CCITT-FALSE over header + payload.

**Frame layout:** `[sync | version | opcode | sequence_id | payload_len | payload | crc16]`

- sync: `0xA5`
- version: `1`
- max payload: 32 bytes

| Opcode | Direction | Payload | Purpose |
|--------|-----------|---------|---------|
| `0x01` KEYBOARD_MATRIX | Arduino → Pi | 2-byte `key_state` | Edge-triggered function keys |
| `0x02` TELEMETRY | Pi → Arduino | 3-byte telemetry | Battery %, temperature, limit flags |
| `0x03` SAFETY | Arduino → Pi | 5-byte fault broadcast | Freefall, comms loss, sensor failure. Pi never sends it |
| `0x04` HEARTBEAT | Pi → Arduino | none | Periodic liveness. Rail stays off until the first one |
| `0x05` ACK_NACK | Reserved | — | Unused on both sides |
| `0x06` CHORD | Arduino → Pi | 1-byte `dot_mask` | Assembled Braille chord |
| `0x07` CLEAR_FAULT | Pi → Arduino | none | Drop latched freefall and comms-loss, then re-apply the rail |

**Telemetry limit flags** (Pi → Arduino relay): `BRAILLATRON_LIMIT_PAPER_EDGE` (TCRT5000), `BRAILLATRON_LIMIT_Y_HOME` (TCST2103), `BRAILLATRON_LIMIT_MOTION_BLOCKED`, `BRAILLATRON_LIMIT_BATTERY_CRITICAL`.

**Safety:** `SAFETY` is Arduino→Pi only (Pi never sends it); emitted codes are FREEFALL, COMMS_LOSS, SENSOR_FAILURE. `FAULT_WATCHDOG_TIMEOUT`, `FAULT_THERMAL`, and `ACK_NACK` are unused/reserved (comms gap uses COMMS_LOSS). Severity INFO through LATCHED (explicit `CLEAR_FAULT`).

Invalid CRC frames are dropped. Pi sends `HEARTBEAT` on the configured interval when the serial device is open.

### 4.3 Two-Layer Watchdog

1. **AVR hardware WDT (500 ms):** A hung main loop resets the MCU. Setup drives D12 low. The rail stays off until the first heartbeat after a good MPU init. A pulldown on the switch enable is still required during reset; see the bring-up to-do.
2. **Host comms watchdog:** Before the first heartbeat the rail stays off. After that, a gap longer than 3 s cuts VMOT and latches `BRAILLATRON_FAULT_COMMS_LOSS` until `CLEAR_FAULT`. The next heartbeat does not restore the rail.

Implementation: `firmware-arduino/src/watchdog.cpp`, `fail_safes.cpp`.

---

## 5. Hardware, Power, Safety & Kinematics

*Board-level power topology and TMC2209 bus layout: [Master Architecture V4.9](Master%20Architecture%20V4.9.md). Prototype pin wiring: [Skeleton Prototype V5.1 Build Guide](Skeleton%20Prototype%20V5.1%20Build%20Guide.md).*

**Retired from earlier specs:** Raspberry Pi 3B, servo-driven 6-key embosser array, 18650 TBD battery, 4×4 keyboard matrix with steering diodes.

### 5.1 Power Distribution

```
[USB-C PD Input] ──► [IP2368 PD Charger]
                              │ BAT+/BAT−
                              │  (PARALLEL on the pack bus — not series with BMS or the load)
                              ▼
[4S 30A BMS P+/P−] ── WAGO / star ── (14.8 V nominal)
         │
         ├─ (15 A motor fuse) ──► [high-side switch] ──► Monster8 VIN+
         │         production: 85 °C thermal fuse on unified heatsink (REQUIRED)
         │         skeleton V5.1: fuse DEFERRED; individual heatsinks
         │         Monster8 VIN− ──► star ground (same net as USB GND; no FET)
         │         Arduino D12 HIGH = switch on
         │         VIN+ ──► 8× TMC2209 VMOT on Monster8
         │
         └─ (5 A logic fuse — V5.1 BOM) ──► [TPS5430 5 V buck] ──┬──► Orange Pi 3B
                                                                  └──► Arduino Micro
                                                                            │
Orange Pi I2S1 ──► [MAX98357A + 470 µF + 0.1 µF local filter] ──► 8 Ω 3 W speaker
```

- **Logic rail:** TPS5430 buck from battery to filtered 5 V for Orange Pi and Arduino.
- **Motor rail:** 14.8 V through a high-side switch to Monster8 VIN+. VIN− ties to star ground. Arduino D12 is active-high enable. A low-side FET on VIN− is bypassed by USB ground.
- **Audio isolation:** MAX98357A powered from 5 V with local 470 µF + 0.1 µF at VDD/GND to keep Class D switching noise off the logic bus.
- **Battery telemetry:** LTC2944 on system I2C tracks capacity, current, and voltage (see §6.3).
- **High-current routing:** Motor VMOT and returns use off-board dual-row terminal blocks (up to 15 A), not prototype-board traces.
- **Thermal fuse:** Production **requires** a non-resettable 85 °C fuse clamped to a unified aluminum heatsink spanning all eight stepper drivers. Skeleton V5.1 is a prototype **without** that fuse (individual heatsinks; V5.1 §2.6). Do not ship a production HAT without it.

### 5.2 Real-Time Hardware Interlock (MPU6050)

- **Sensor:** MPU6050 on Arduino hardware I2C (SDA/SCL); freefall thresholds configured in hardware registers (`FF_THR` / `FF_DUR`) at boot.
- **Interrupt:** MPU6050 INT → Arduino **D7** (PE6 / INT6), **active-low latched**, ISR on **FALLING**. **Do not wire INT to D3** — D3 is SCL (**INT0**). Firmware `INT_PIN_CFG = 0xA0`. V9 historically said active-high / INT0; that polarity **misses freefall**. GY-521-style breakouts that default INT to active-high must be reconfigured.
- **Gate drive:** high-side switch on Monster8 VIN+, enabled by Arduino D12 (see [Skeleton Prototype V5.1 Build Guide](Skeleton%20Prototype%20V5.1%20Build%20Guide.md) §2.7). The part is not selected.

**Freefall path:**

1. The MPU free-fall counter (`FF_THR` / `FF_DUR`, about 20 ms in firmware) qualifies the drop, then INT goes **low** and latches.
2. INT6 **FALLING** ISR runs (bypasses keyboard polling).
3. ISR drives D12 low. The ISR does **not** transmit serial. The main loop re-asserts that hold.
4. The **main loop** transmits `BRAILLATRON_OP_SAFETY` with `BRAILLATRON_FAULT_FREEFALL` (`braillatron_app.cpp`).
5. Pi `keyboard_service` blocks **MotionGate** and issues Klipper emergency stop when Klipper is enabled; Output Hub alerts the user. Comms loss and sensor failure take the same stop.

### 5.3 Dual-Bus TMC2209 UART Daisy Chain — RETIRED (Option A)

**Canonical motion path:** MKS Monster8 V2 + Klipper over USB ([Skeleton Prototype V5.1 Build Guide](Skeleton%20Prototype%20V5.1%20Build%20Guide.md)). The Pi-native UART4/UART9 daisy chain below is **not wired** on new builds.

```
[RETIRED]
Orange Pi UART4 ──► TMC2209 drivers 1–4
Orange Pi UART9 ──► TMC2209 drivers 5–8
```

### 5.4 Staggered Embossing Head (six NEMA14 — not solenoids)

**Do not order solenoids.** Live hardware is six **NEMA14 steppers** on Monster8 slots 2–7 (`manual_stepper emboss_1…6` in `klipper/printer.cfg`). Solenoid-stagger language in earlier V9 drafts is retired.

Standard cells: left column dots 1–3, right column dots 4–6. Physical layout:

- **Row A (top):** NEMA14 punches for dots 1, 3, 5 (slots 2, 4, 6).
- **Row B (bottom):** NEMA14 punches for dots 2, 4, 6 (slots 3, 5, 7).
- **Spatial offset:** 2.5 mm along the X-axis (carriage path). `EmbossScheduler` uses that fixed pitch. It does not add crank dwell.

The motion controller must not fire all punches simultaneously. Row A fires at the current travel-log position; Row B is buffered and fired 2.5 mm further along +X. Module: `emboss_scheduler.cpp`. Slot map, pin names, and `run_current` live **only** in `printer.cfg`. Punch stroke in millimetres is still a placeholder (`emboss_stroke_mm=2.0` on `rotation_distance` 40).

### 5.5 Stepper Drivers, Homing & Paper Sensing

- **Drivers:** Eight TMC2209 on Monster8 — slot 0 X carriage, slot 1 Y tractor, slots 2–7 six NEMA14 punch actuators. Pin names, endstops, and `run_current` live **only** in `klipper/printer.cfg`.
- **Y-axis homing:** TCST2103 optical slot sensor on **Y-STOP `^PA15`** — reverse feed until triggered; set Y = 0.
- **Paper edge / page boundary:** TCRT5000 reflective IR on **X-STOP `^PA14`** (`printer.cfg`). X is never G28-homed. Do **not** wire this sensor to E0-STOP or FIL_RUNOUT.

### 5.6 Engineering Constraints & Mitigations

| Constraint | Risk | Mitigation |
|------------|------|------------|
| Heavy stepper EMI | Audio instability, SoC noise | Digital I2S audio (MAX98357A); local 470 µF + 0.1 µF on amp VDD/GND |
| RK3566 pin limits | Cannot wire 8 independent driver UARTs | **MKS Monster8 V2 + Klipper over USB** — Pi issues motion via Moonraker, not Pi UART (§5.3) |
| Sudden power loss | eMMC/SD corruption | Read-only root + tmpfs volatile mounts; atomic writes to `/data`; `braillatron-sync.timer` |
| Drop during motion | Head / NEMA14 punch damage | MPU6050 hardware interrupt (D7/INT6, active-low) → ISR drives D12 low; SAFETY frame from the **main loop**. Cut is high-side on VIN+ |
| Driver thermal runaway | Fire / hardware damage | Production: unified heatsink + **required** 85 °C thermal fuse on motor rail. Skeleton V5.1 defers the fuse. |
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
- **`getty@tty1` stays enabled** and launches the Braillatron UI instead of a login shell. Serial getty is masked.
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

> **A/B OTA is not implemented.** There is no RAUC, Mender, dual-bank, or over-the-air update path in this repo. Current field updates are `deploy/install.sh` on an existing DietPi image, or a full SD/eMMC image refresh (Pi SD Image Software Build Guide). Read-only root + `/data` transactional storage (`deploy/os/setup-overlay-ro.sh`) is persistence, not an OTA mechanism. Do not treat this section as a planned implementation here.

### 6.6 Dependencies

- **TTS:** eSpeak NG (Speech Dispatcher) over **ALSA** (aux default; BlueALSA for Bluetooth). Piper excluded. Not PipeWire.
- **STT:** Vosk-API + **ALSA** capture (skeleton: Pi 3.5 mm jack). Production PDM MEMS (ICS-43432) has no V5.1 pinout.
- **Braille:** liblouis (UEB G1/G2, Nemeth).
- **Embosser:** C++ kinematics daemon (`motion_controller`, `emboss_scheduler`).
- **Dictionary:** SQLite 3 (`libsqlite3`, `sqlite3` CLI for data install).
- **connectd media:** mpv (shared IPC via `/run/braillatron/mpv.sock`), yt-dlp, ffmpeg.
- **connectd network:** curl; signal-cli (Messages); OAuth device flow for Gmail (no Google SDK).
- **Offline data install:** `braillatron-install-dictionary-data`, `braillatron-install-spelling-data` (see Pi SD Image guide).

### 6.7 Wi‑Fi and network stack

Production images use **DietPi ifupdown + wpa_supplicant** on **`wlan0`**, not NetworkManager. Bootstrap runs `deploy/os/setup-dietpi-networking.sh`:

- **`ifup@wlan0.service`** brings up Wi‑Fi at boot (disable with `BRAILLATRON_WIFI_BOOT=0` for Ethernet-only benches)
- **NetworkManager** and **`dietpi-wifi-monitor.service`** are disabled/masked to avoid conflicts
- Credentials persist in **`/etc/wpa_supplicant/wpa_supplicant.conf`** (`update_config=1`)

| Layer | Role |
| --- | --- |
| **OS / systemd** | `ifup@wlan0`, `ifup@eth0`/`end0`; `network-online.target` for display sidecars |
| **wpa_supplicant** | Association, roaming, saved networks |
| **Network and Devices app** | On-device scan/connect via `wpa_cli` (`network_app.cpp`) |
| **Factory helper** | `deploy/os/setup-wifi-credentials.sh` — SSH provisioning before or after bootstrap |
| **Quick Status** | Reads connected SSID from `wpa_cli -i wlan0 status` (`output_hub.cpp`) |
| **connectd** | Network *apps* (YouTube, Weather, Gmail, …) — separate sidecar; requires IP connectivity but does not manage Wi‑Fi |

Appliance boot ordering keeps **`getty@tty1`** ahead of slow Wi‑Fi bring-up so a late tty1 init does not wipe the framebuffer. Shipped `display.conf` sets `hdmi_enabled=true`, so a connected HDMI monitor shows UI chrome. See Pi SD Image guide **Wi‑Fi and network connectivity**.

> **Legacy:** `deploy/os/setup-networkmanager.sh` is retained for manual recovery only — do not run on current images.

---

## 7. Standardized Hardware Reference

| Subsystem | Standardized Part |
|-----------|-------------------|
| SBC | Orange Pi 3B (4 GB LPDDR4, RK3566, WiFi/BT) |
| Motion controller | MKS Monster8 V2 (Klipper MCU, USB to Pi) |
| Co-processor | Arduino Micro (ATmega32U4, 5 V, native USB) |
| PD input / charge | IP2368 USB-C PD charger, **parallel** on WAGO/star (not series with BMS) |
| Battery | 4S1P Molicel P28A (14.8 V nominal) + 4S BMS with active balancing |
| Logic power | TPS5430 synchronous buck (5 V) |
| Safety interlock | High-side switch on Monster8 VIN+, Arduino D12 active-high enable. Part not selected |
| Battery gas gauge | LTC2944 (I2C coulomb counter) |
| Audio amp | MAX98357A I2S Class D mono (Rockchip I2S1 bypass) |
| Internal speaker | 8 Ω 3 W enclosed capsule (foam-isolated) |
| Audio filter | 470 µF low-ESR electrolytic + 0.1 µF ceramic at amp VDD/GND |
| Haptic driver | DRV2605L I2C |
| Haptic actuator | LRA (linear resonant actuator) |
| Paper edge sensor | TCRT5000 reflective IR |
| Y-axis homing | TCST2103 optical slot (transmissive) |
| Stepper drivers | 8× TMC2209 on Monster8 (slots 0–7, §5.3); pin map in `printer.cfg` |
| Emboss actuators | 6× NEMA14 steppers (slots 2–7) — **not solenoids** |
| Freefall sensor | MPU6050 (Arduino I2C + INT on **D7 / PE6 / INT6**, active-low latched). **Not INT0** (INT0 is D3/SCL). |
| Keyboard switches | 12× Cherry MX (direct pin, §1.3) |

**Production-only / unspecified on skeleton V5.1 (do not invent pinouts):** PDM MEMS mic ICS-43432, grounded copper cage, custom HAT netlist, DRV2605L **EN** (breakout assumed strapped high).

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
| Brailler + edit FSM | Partial — full-cell replace works; the three named modes are not selected by the UI | `brailler_app.cpp`, `edit_session.cpp` |
| Document dictation (PTT → BRF) | Implemented | `brailler_app.cpp`, Settings toggle |
| Coordinate memory | Implemented | `coordinate_state.cpp` |
| Boot homing | Implemented | `homing_service.cpp` |
| Calculator Nemeth | Implemented | `calculator_app.cpp` |
| Transcriber pipeline | Implemented | `transcriber_app.cpp`, Vosk backend |
| Morse learning / output | Implemented | `morse_encoder.cpp`, inline + standalone apps |
| Factory Test app | Implemented | `factory_test_app.cpp` |
| Network Wi-Fi | Implemented (wpa_supplicant / `wpa_cli`) | `network_app.cpp` |
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
| Library / LocalSend | Library implemented (EPUB/DAISY/Gutendex; BARD/Bookshare deferred). LocalSend receive path exists and is not started by `braillatron.target` until the unit is enabled | `library_app.cpp`, `localsend_app.cpp` |
| Gmail | OAuth inbox/read/send implemented (needs device validation). IMAP-linked accounts can read; SMTP send is not wired | `gmail_app.cpp`, `gmail_backend.cpp` |
| Inter-processor protocol v1 | Implemented | `shared/protocol.h`, firmware + daemon parsers |
| Telemetry JSON bridge | Implemented | `telemetry_bridge.cpp` |
| 20% battery warning | Implemented | `telemetry_sentinel.cpp`, UI poll |
| Crash reporter | Implemented (optional build) | `crash_reporter.cpp` |
| OTA A/B | **Not implemented** (`install.sh` / image refresh; §6.5) | — |
| Piper TTS | **Excluded** | — |

**Connectivity follow-up:** See [Connectivity Follow-Up Checklist](Connectivity%20Follow-Up%20Checklist.md).
