# The Graham Brailler: Hardware Engineering Blueprint & PCB Lifecycle (V4.9)

**Lead Architect:** Addison Graham

**Target Hardware:** Orange Pi 3B (SBC) + Custom PCB (HAT)

**Operating System:** DietPi Linux (Debian 13 Trixie, Vendor Kernel 6.1.115)

*Hardware and PCB lifecycle companion to [Master Software Architecture V9](Master%20Software%20Architecture%20V9.md). V9 is the canonical spec for product behavior, applications, co-processor protocol, and implementation status. This document focuses on breadboard-to-PCB transition, power topology, driver bus layout, and board-level mitigations.*

---

## Document map

| Topic | Primary doc |
|-------|-------------|
| Applications, ScreenReader UI, edit modes, implementation status | [Master Software Architecture V9](Master%20Software%20Architecture%20V9.md) |
| Arduino ↔ Pi wire protocol (opcodes, frames, safety) | [V9 §4](Master%20Software%20Architecture%20V9.md#4-co-processor--inter-processor-protocol), `shared/protocol.h`, `shared/protocol.md` |
| Prototype wiring, GPIO, I2S, Monster8/Klipper | [Skeleton Prototype V5.1 Build Guide](Skeleton%20Prototype%20V5.1%20Build%20Guide.md) |
| DietPi image, overlayfs, systemd, deploy | [Pi SD Image Software Build Guide](Pi%20SD%20Image%20Software%20Build%20Guide.md) |
| Power rails, thermal fuse, Monster8 motion path (this doc) | §3–4 below |

---

## 1. Core concept & operating scope

The Graham Brailler is a portable, 3D-printable electromechanical Smart Braille Notetaker and Embosser: a standalone headless DietPi device (Debian 13 Trixie, vendor kernel 6.1.115 for RK3566 peripheral stability).

V4.9 marks the transition from breadboard concepts to custom PCB manufacturing. Software scope (editor, calculator, BARD/Bookshare, TTS/STT pipelines, app registry) is defined in [V9 §1–3](Master%20Software%20Architecture%20V9.md).

**Platform dependencies (unchanged at hardware level):**

- Offline TTS: eSpeak NG via PipeWire and Speech Dispatcher
- Offline STT: Vosk-API with Rockchip PDM capture
- Embosser motion: C++ daemons on Orange Pi; TMC2209 steppers on motor rail

---

## 2. UI hardware layer (ScreenReader paradigm)

Hardware must support the universal ScreenReader paradigm described in [V9 §1](Master%20Software%20Architecture%20V9.md#1-product-vision--screenreader-paradigm). Board-level requirements:

### 2.1 Keyboard (direct pin — production)

**Retired:** 4×4 switch matrix, 1N4148 steering diodes per key, external 10 kΩ pull-ups.

**Production:** 12 Cherry MX tactile switches (6 Braille dots, D-pad Up/Down, Backspace, Enter, Shift/TTS, Speech) wired direct-pin to the Arduino Micro — one GPIO per key, common ground bus, `INPUT_PULLUP`, active LOW. **Menu** is a software overlay (backtick / remote display), not a 13th physical key. Scanning, debounce, and chord assembly run on the co-processor ([V9 §1.3](Master%20Software%20Architecture%20V9.md#13-production-keyboard-driver-direct-pin-topology), Build Guide Part 3.1).

### 2.2 Audio (I2S Class D)

System audio over Rockchip **I2S1** to **MAX98357A** Class D mono amp (on-silicon DAC + amplification, up to ~3.2 W into an **8 Ω 3 W** enclosed capsule). Private listening via Orange Pi 3.5 mm jack.

Local **470 µF + 0.1 µF** at MAX98357A VDD/GND (see §3.1).

### 2.3 Microphone (PDM MEMS)

Digital PDM MEMS (e.g. ICS-43432) on the Top UI board inside a grounded copper-tape cage to reduce capacitive coupling and stepper EMI; routed to Rockchip PDM.

### 2.4 Haptics

**DRV2605L** I2C driver + **10 mm LRA** for navigation boundaries, errors, Morse patterns, and deaf-blind tactile feedback. Pi-side haptic commands and menu policy: [V9 §1.2](Master%20Software%20Architecture%20V9.md#12-universal-outputs-distribution-hub).

---

## 3. Compute & power infrastructure

Block diagram for custom PCB and HAT routing:

```
[USB-C PD Input]
         │
         ▼
[IP2368 PD Charger]
         │
         ▼
[4S 30A BMS w/ Balancer] (14.8 V nominal)
         │
         ├─────────────────────────────────┐
         ▼ (15 A motor fuse)               ▼ (5 A logic fuse — see V5.1 Part 1 BOM)
   [85 °C thermal fuse]                     ▼
         │                        [Mini560 / TPS5430 5 V buck]
         ▼                                 │
   [IRLZ44N MOSFET]                         ├──────────────► [Orange Pi 3B]
   (low-side on Monster8 VIN−)              └──────────────► [Arduino Micro]
         │
         ▼
[15 A star power terminal block]
         ├──► Monster8 VIN+ → 8× TMC2209 VMOT
         └──► star ground return

Orange Pi I2S1 ──► [MAX98357A + local filter] ──► [8 Ω 3 W speaker]
```

### 3.1 Structural safety interlocks

- **High-current terminals:** VMOT and returns use dual-row terminal blocks (up to 15 A), not prototype-board traces.
- **Thermal fuse:** Optional on skeleton (individual heatsinks). Production target: unified aluminum bar + 85 °C fuse on motor rail (§3.1).
- **Motor rail gate:** **IRLZ44N low-side** on Monster8 VIN− return (Drain → VIN−, Source → star ground); **TC4420** gate driver from Arduino D12. Cut on freefall, comms loss, or watchdog fault ([V9 §5.2](Master%20Software%20Architecture%20V9.md#52-real-time-hardware-interlock-mpu6050), `shared/protocol.h`). Pi also issues Klipper **M112** on freefall SAFETY frames.
- **Audio filtering:** 470 µF low-ESR + 0.1 µF ceramic at MAX98357A VDD/GND to keep Class D switching noise off the 5 V logic bus.

### 3.2 Battery & telemetry

**LTC2944** on system I2C: capacity, current, voltage. Policy at 20% / 5% SOC: [V9 §6.3](Master%20Software%20Architecture%20V9.md#63-battery-policy). Pi relays limit flags to Arduino via `BRAILLATRON_OP_TELEMETRY`.

---

## 4. Motion control — Option A: Monster8 + Klipper (canonical)

> **Errata (2025):** The dual-bus **Orange Pi UART4/UART9 TMC2209 daisy chain** described in earlier revisions is **retired** for the skeleton and production path. Eight TMC2209 drivers live on the **MKS Monster8 V2**, flashed as a **Klipper MCU**, connected to the Pi via **USB**. Motion, homing, endstops, and `run_current` tuning are configured in Klipper `printer.cfg`. See [Skeleton Prototype V5.1 Build Guide](Skeleton%20Prototype%20V5.1%20Build%20Guide.md) for driver slot map and wiring.

The historical UART daisy-chain diagram is preserved below for PCB archaeology only — **do not wire** on new builds:

```
[RETIRED — Pi-native path]
Orange Pi UART4 ──┬──► TMC2209 Driver 1 (addr 0)
                  ├──► TMC2209 Driver 2 (addr 1)
                  ├──► TMC2209 Driver 3 (addr 2)
                  └──► TMC2209 Driver 4 (addr 3)

Orange Pi UART9 ──┬──► TMC2209 Driver 5 (addr 0)
                  ├──► TMC2209 Driver 6 (addr 1)
                  ├──► TMC2209 Driver 7 (addr 2)
                  └──► TMC2209 Driver 8 (addr 3)
```

Pin-level wiring for the active stack: [Skeleton Prototype V5.1 Build Guide](Skeleton%20Prototype%20V5.1%20Build%20Guide.md). Motion, homing, and stagger timing: [V9 §3.3–3.4, §5.4–5.5](Master%20Software%20Architecture%20V9.md).

---

## 5. Co-processor integration (board-level)

The **Arduino Micro** (Tier 3) isolates real-time safety from the Orange Pi:

| Function | Hardware |
|----------|----------|
| Keyboard scan / debounce / chords | 12 direct-pin GPIOs (+ Menu via software overlay) |
| Freefall interlock | MPU6050 → INT6 (D7); ISR cuts IRLZ44N in <10 ms |
| Host liveness | Pi `HEARTBEAT` over USB CDC; comms timeout cuts VMOT |
| MCU hang recovery | AVR 500 ms hardware WDT |

Protocol and opcode definitions: `shared/protocol.h`, [V9 §4](Master%20Software%20Architecture%20V9.md#4-co-processor--inter-processor-protocol).

---

## 6. Storage layout (hardware resilience)

Sudden power cuts from the safety interlock must not corrupt the OS partition:

- **Read-only root (`/`):** OS, daemons, and apps on a locked partition; volatile changes via **overlayfs** in RAM.
- **Transactional `/data/`:** User BRF files, settings, and databases on a dedicated read-write partition.
- **Atomic writes:** RAM buffer → `.tmp` → `fsync` → `rename`; **`braillatron-sync.timer`** mirrors RAM layers to flash.

Full deploy procedure: [Pi SD Image Software Build Guide](Pi%20SD%20Image%20Software%20Build%20Guide.md). Software policy: [V9 §6.1](Master%20Software%20Architecture%20V9.md#61-read-only-root--persistent-data).

> **OTA / A/B updates:** RAUC or Mender dual-bank OTA is **not yet implemented**. Current field updates use read-only root + `/data` transactional storage (`deploy/os/setup-overlay-ro.sh`). See [V9 §6.5](Master%20Software%20Architecture%20V9.md#65-ota--ab-updates--not-yet-addressed).

---

## 7. Engineering constraints & mitigations

| Constraint | Risk | Mitigation |
|------------|------|------------|
| Heavy stepper EMI | Audio hum, SoC instability | Digital I2S (MAX98357A); local 470 µF + 0.1 µF on amp |
| RK3566 pin limits | Cannot wire 8 independent driver UARTs | **MKS Monster8 V2 + Klipper over USB** — Pi issues motion via Moonraker, not Pi UART (§4) |
| Sudden power loss | eMMC/SD corruption | Read-only root, overlayfs, atomic `/data` writes, sync timer (§6) |
| Drop during motion | Head/solenoid damage | MPU6050 hardware INT → sub-10 ms IRLZ44N cut + SAFETY frame (§5) |
| Driver thermal runaway | Fire / hardware damage | Unified heatsink + 85 °C thermal fuse on motor rail (§3.1) |
| Multi-key Braille chords | Ghost keys (legacy matrix) | **Direct-pin keyboard** — one GPIO per key, no matrix (§2.1) |

Standardized BOM: [V9 §7](Master%20Software%20Architecture%20V9.md#7-standardized-hardware-reference). Prototype breadboard part list and fuse ratings: [Skeleton Prototype V5.1 Build Guide](Skeleton%20Prototype%20V5.1%20Build%20Guide.md) Part 1.

---

## 8. Retired hardware concepts

| Retired | Superseded by |
|---------|----------------|
| Raspberry Pi 3B | Orange Pi 3B |
| Servo-driven 6-key embosser array | Staggered solenoid head ([V9 §5.4](Master%20Software%20Architecture%20V9.md#54-staggered-embossing-head)) |
| 18650 TBD battery pack | 4S LiPo + LTC2944 |
| 4×4 keyboard matrix + per-key diodes | Direct-pin Arduino topology (§2.1) |
| Piper TTS | eSpeak NG ([V9 §6.6](Master%20Software%20Architecture%20V9.md#66-dependencies)) |

---

## Related documentation

- [Master Software Architecture V9](Master%20Software%20Architecture%20V9.md) — canonical product and software specification
- [Skeleton Prototype V5.1 Build Guide](Skeleton%20Prototype%20V5.1%20Build%20Guide.md) — prototype wiring and keyboard Part 3.1
- [Pi SD Image Software Build Guide](Pi%20SD%20Image%20Software%20Build%20Guide.md) — DietPi image, overlayfs, systemd
- `shared/protocol.h` / `shared/protocol.md` — inter-processor frame format
- `deploy/` — `bootstrap-dietpi.sh`, `install.sh`, systemd units, OS scripts
