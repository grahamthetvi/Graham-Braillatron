# Master Software Architecture & Engineering Specification V9

**Project:** Mobile Smart Braille Notetaker & Embosser (Graham Brailler)

**Lead Architect:** Addison

**Target Hardware:** Orange Pi 3B (SBC) + Custom PCB (HAT)

**Operating System:** DietPi Linux (Debian 13 Trixie, Vendor Kernel 6.1.115)

**Co-Processor:** Arduino Micro (ATmega32U4, 5V Logic)

*This document merges V4 product architecture with V8 engineering truth. Sections 4–5 and 7 defer to V8 for full electrical and kinematic detail.*

---

## 1. Product Vision & ScreenReader Paradigm

The Graham Brailler is a portable electromechanical Smart Braille Notetaker and Embosser running headless DietPi. A single **ScreenReader UI Paradigm** (focus-based linear navigation) serves blind, low-vision, and deaf-blind users with identical underlying logic; the **Output Hub** routes focused content to the user's preferred channels.

### 1.1 Universal Inputs (Concurrent)

- **Built-in Braille Keyboard:** 6-key Perkins layout, D-pad (Up/Down), Backspace, Enter, Shift/TTS, Speech (PTT), Menu.
- **Peripheral QWERTY:** USB/Bluetooth; Windows/Super = Menu, Win+H = Dictation.
- **Refreshable Braille Displays:** USB/Bluetooth via BRLTTY (e.g. Mantis Q40).
- All inputs process concurrently without locking out others.

### 1.2 Universal Outputs (Distribution Hub)

| Channel | Hardware / Software | Module |
|---------|---------------------|--------|
| TTS | eSpeak NG via Speech Dispatcher; MAX98357A I2S + 3.5 mm jack | `output_hub.cpp`, `backend.cpp` |
| Refreshable Braille | BRLTTY brlapi + liblouis forward translation | `backend.cpp`, `liblouis_bridge.cpp` |
| Embosser | Solenoid stagger head via `MotionService` | `motion_service.cpp`, `emboss_scheduler.cpp` |
| Haptics | DRV2605L LRA; Morse timed pulses | `drv2605l.cpp`, `morse_encoder.cpp` |

**Deaf-blind menu parity:** When TTS is disabled and `deaf_blind_menu_parity` is enabled, the Output Hub embosses full menu text (no abbreviations) in addition to refreshable braille/haptics.

### 1.3 Keyboard Driver (Direct Pin — V8 Truth)

Direct-pin topology on Arduino Micro: 13 keys, 1 kHz scan, 15 ms integrator debounce, 40 ms chord assembly. See V8 §1.2 and Skeleton Prototype V5.1 Build Guide Part 3.1. The legacy 4×4 diode matrix is **retired**.

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
| **Library** | BARD, Bookshare, public domain (scaffold) | `apps/library_app.cpp` |
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

When switching Standalone apps: reverse to paper-edge sensor, measure page, feed to fresh page. Module: `paper_separator.cpp`.

---

## 4. Co-Processor & CDC Serial Protocol

*Unchanged from V8 §2–3.* Arduino Micro handles keys, debounce, chords, MPU6050 freefall interlock. 4-byte fixed CDC packets at 115200 bps. Protocol source: `shared/protocol.h`.

---

## 5. Hardware, Power, Safety & Kinematics

*Unchanged from V8 §4–5 and Master Architecture V4.9 §3.* Orange Pi 3B, 4S LiPo 14.8V, LTC2944, IRLZ44N interlock, TMC2209, staggered solenoid head, spatial delay line.

**Retired from earlier specs:** Raspberry Pi 3B, servo-driven 6-key embosser array, 18650 TBD battery (superseded by 4S LiPo + LTC2944).

---

## 6. OS, Storage & Telemetry Policy

### 6.1 Read-Only Root & Persistent `/data`

- Root `/` read-only with overlayfs for volatile paths.
- User documents and settings on `/data/braillatron/`.
- Atomic writes: RAM buffer → `.tmp` → `fsync` → `rename`.
- `braillatron-sync.timer` mirrors RAM layers to flash.

### 6.2 Battery Policy

| Threshold | Action |
|-----------|--------|
| 20% | One-time audio + haptic warning per session (UI reads `/run/braillatron/telemetry.json`) |
| 5% | Block motion, flush RAM/coords/BRF, shutdown haptic, graceful power off |

### 6.3 Crash Reporting (Optional)

Sentry / Memfault via `crash_reporter.cpp`. Disabled when DSN/keys empty. **Never** attach document text, SSIDs, or user paths — stack traces and hardware metrics only.

### 6.4 OTA / A/B Updates — NOT YET ADDRESSED

> **Footnote:** Over-the-air A/B dual-bank updates (RAUC or Mender) are **not yet addressed**. Current deployment uses DietPi read-only root + `/data` transactional storage (`deploy/os/setup-overlay-ro.sh`). Future work must define partition layout and choose RAUC or Mender before client implementation.

### 6.5 Dependencies

- **TTS:** eSpeak NG (Speech Dispatcher) — Piper excluded.
- **STT:** Vosk-API + PipeWire capture.
- **Braille:** liblouis (UEB G1/G2, Nemeth).
- **Embosser:** C++ kinematics daemon (`motion_controller`, `emboss_scheduler`).

---

## 7. Standardized Hardware Reference

*See V8 §7 table.* Orange Pi 3B, Arduino Micro, TPS5430, IRLZ44N, LTC2944, MAX98357A, DRV2605L, TCRT5000, TCST2103.

---

## Appendix A — Implementation Status Matrix

| Spec Item | Status | Module |
|-----------|--------|--------|
| ScreenReader focus nav | Implemented | `focus_nav.cpp` |
| Menu overlay | Implemented | `menu_overlay.cpp` |
| Output Hub TTS/BRL/STT/Haptics | Implemented | `output_hub.cpp` |
| Embosser output channel | Implemented | `EmbosserBackend`, `motion_service.cpp` |
| Deaf-blind menu parity | Implemented | `OutputHub::emit` policy |
| App registry / Standalone-Inline | Implemented | `app_registry.cpp` |
| Brailler + edit FSM | Implemented | `brailler_app.cpp`, `edit_session.cpp` |
| Coordinate memory | Implemented | `coordinate_state.cpp` |
| Boot homing | Implemented | `homing_service.cpp` |
| Calculator Nemeth | Implemented | `calculator_app.cpp` |
| Transcriber pipeline | Implemented | `transcriber_app.cpp`, Vosk backend |
| Morse learning / output | Implemented | `morse_encoder.cpp`, inline + standalone apps |
| Network Wi-Fi | Implemented | `network_app.cpp` |
| connectd sidecar | Implemented (needs device validation) | `connect/`, `braillatron-connectd.service` |
| YouTube audio app | Implemented (needs device validation) | `youtube_app.cpp`, `youtube_backend.cpp` |
| Signal messaging app | Implemented (needs device validation) | `messages_app.cpp`, `signal_backend.cpp` |
| Library / LocalSend | Scaffold | `library_app.cpp`, `localsend_app.cpp` |
| Telemetry JSON bridge | Implemented | `telemetry_bridge.cpp` |
| 20% battery warning | Implemented | `telemetry_sentinel.cpp`, UI poll |
| Crash reporter | Implemented (optional build) | `crash_reporter.cpp` |
| OTA A/B | **Not addressed** | — |
| Piper TTS | **Excluded** | — |

**Connectivity follow-up:** See [Connectivity Follow-Up Checklist](Connectivity%20Follow-Up%20Checklist.md).
