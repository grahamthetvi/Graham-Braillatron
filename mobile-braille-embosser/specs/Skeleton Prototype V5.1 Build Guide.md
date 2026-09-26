# **Skeleton Prototype V5.1 Build Guide**

**Project Lead:** Addison Graham | **Phase:** 2-Month "Coffee Table" Breadboard & 3-Tier Architecture Integration

This V5.1 Prototype Guide is the **canonical wiring document** for the Graham Braillatron skeleton. It integrates the industrial-grade 3-tier architecture:

| Domain | Source of truth |
|--------|-----------------|
| Power / keys / MPU / VMOT gate | **This guide** + `firmware-arduino/src/pins.h` — 12 physical keys, MPU INT **active-low latched FALLING on D7**, IP2368 **in parallel** on the WAGO bus, D12 high-side enable. Bench work that is still open: [Hardware Bring-Up To-Do](Hardware%20Bring-Up%20To-Do.md) |
| Monster8 pins / endstops / currents | `klipper/printer.cfg` **only** (Part 5). Do not copy pin excerpts from V9/V4.9. |

V9 remains the software spec. Solenoid heads, MPU INT0 / active-high INT, series USB-C → IP2368 → BMS, and E0-STOP paper-edge wiring are retired.

| Tier | Board | Role |
|------|-------|------|
| 1 | Orange Pi 3B | Brain — DietPi, apps, Moonraker/Klipper host, I2S audio, I2C telemetry |
| 2 | MKS Monster8 V2 | Klipper MCU — 8× TMC2209 stepper drivers, endstops, real-time motion |
| 3 | Arduino Micro | Safety watchdog — 12-key scan, MPU6050 freefall, VMOT gate, USB CDC |

**Option A (long-term):** Monster8 + Klipper over USB. The retired Pi-native UART4/UART9 TMC2209 daisy-chain path is documented only as historical errata in [Master Architecture V4.9](Master%20Architecture%20V4.9.md).

---

## **Part 1: Comprehensive Bill of Materials (BOM)**

### **1. High-Power & Safety Bus (Off-Breadboard)**

| Component | Specifications / Description | Qty Required |
| :---- | :---- | :---- |
| **Power Source** | 4S1P Molicel P28A pack (14.8 V nominal, 16.8 V max). Commercial holders with Beryllium Copper contacts rated 15 A+. | 1 Pack |
| **Battery Management (BMS)** | HiLetgo 4S 30 A Lithium BMS with active cell balancing. | 1 Board |
| **Charge/Boost Controller** | IP2368 USB-C PD module (bi-directional, 100 W). | 1 Module |
| **USB-C Panel Port** | Panel-mount USB-C receptacle (PD input) wired to IP2368 with short pigtails. | 1 Jack |
| **Main Motor Fuse** | 15 A ATC inline blade fuse on 14.8 V motor rail. | 1 Fuse |
| **Logic Fuse** | 5 A ATC inline blade fuse on 14.8 V buck input. | 1 Fuse |
| **Thermal Cutoff Fuse** | 85 °C non-resettable thermal fuse — **not installed on this skeleton prototype** (see Part 2.6). Production HAT **must** include it. | 0 on skeleton / 1 on production |
| **Power Distribution** | 12-position dual-row screw terminal strip (star ground). 5-port WAGO lever nut (positive bus). | 1 Strip, 1 Nut |
| **Motor-rail switch** | High-side break in Monster8 **VIN+**, after the 15 A fuse. Part is not selected. Arduino D12 is active-high enable (HIGH = motors on). Do **not** put an IRLZ44N in VIN− — USB ground bypasses it. See [Hardware Bring-Up To-Do](Hardware%20Bring-Up%20To-Do.md). | 1 |
| **Spike Protection** | TVS diode 18–20 V clamp (e.g. SMBJ18A or P6KE18CA) — **optional for bench** (see Part 2.5). | 0–1 Diode |
| **Heavy-Gauge Wire** | #12–#14 AWG solid copper for pack/BMS/motor returns; #18–#22 for logic. | 1 Spool |

### **2. Compute, Logic & UI Components**

| Component | Specifications / Description | Qty Required |
| :---- | :---- | :---- |
| **Primary Processor (Tier 1)** | Orange Pi 3B (4 GB LPDDR4, Rockchip RK3566). On-board Wi‑Fi/BT silicon is whatever that **board revision ships** — this repo does not pin a module PN. | 1 Unit |
| **Motion Controller (Tier 2)** | MKS Monster8 V2 (32-bit STM32, Klipper MCU). | 1 Board |
| **Safety Watchdog (Tier 3)** | Arduino Micro (ATmega32U4, 5 V native). | 1 Unit |
| **Logic Power Supply** | Mini560 (TPS5430) buck — 14.8 V in, set to **5.1 V** under light load before the Pi is attached. | 1 Module |
| **Audio Amplifier** | MAX98357A I2S Class D mono breakout. | 1 Board |
| **Integrated Speaker** | 8 Ω 3 W enclosed micro speaker capsule (primary). 3.5 mm lapel mic on Pi aux (dev/STT). Bluetooth audio optional. | 1 Capsule |
| **Audio Filter Capacitors** | 470 µF 35 V electrolytic + 0.1 µF ceramic at MAX98357A. | 1 Set |
| **Switch bypass** | 0.1 µF ceramic at the high-side switch logic supply, once that part is chosen. | 1 |
| **Audio SD Resistor** | 100 kΩ pull-up on MAX98357A SD pin. | 1 Unit |
| **Haptic UI Driver** | DRV2605L I2C + 10 mm LRA. | 1 Set |
| **User Display** | ST7789 240×240 SPI panel (3.3 V logic). | 1 Panel |
| **Sensors** | MPU6050 (Arduino I2C), LTC2944 (Pi I2C), TCRT5000 (paper edge), TCST2103 (Y home). | 1 Set |
| **Keyboard** | 12× Cherry MX tactile switches — direct-pin to Arduino (no matrix, no diodes). | **12 Switches** |
| **EMI Suppression** | Ferrite rings on motor bundles and Pi↔Monster8 USB. | 1 Set |

### **3. Motor Driver & Motion Components**

| Component | Specifications / Description | Qty Required |
| :---- | :---- | :---- |
| **Motor Drivers** | TMC2209 StepStick (in Monster8 slots 0–7). | 8 Boards |
| **X-Axis Carriage** | NEMA 17 slim — **17HS08-1004S** (short body, ~1.0 A). | 1 Motor |
| **Y-Axis Paper Feed** | NEMA 17 — **17HS15-1504S** (~1.5 A). | 1 Motor |
| **Embossing Actuators** | NEMA 14 steppers, dots 1–6 (Row A: 1,3,5; Row B: 2,4,6). | 6 Motors |

**Paper:** 100 lb cardstock, 0.5 in perf spacing, 33-line fresh page feed on app switch.

### Mechanical CAD gap

This guide specifies **electrical interconnect** (power, GPIO, Monster8 headers, Arduino pins). Mechanical CAD and actuator manufacturer part numbers are **not in this repo**. Missing: NEMA14 SKU, coil/phase order, GT2 belt, tractor sprocket, crank/linkage, chassis, keyboard plate, and embossing head. Do not invent those PNs — you cannot fabricate the machine from this repository.

### Unspecified on skeleton

The following are **not specified** for this skeleton (do not invent CAD, photos, or PNs):

- **Speech key seat** — electrical pin is Arduino **A3** (Part 3.1); physical seat / plate location is not drawn.
- **Cherry MX colorway** — BOM calls for 12 tactile switches; switch color / SKU is not pinned.
- **Monster8 UART jumper photos** — Part 3.2 requires UART jumpers under each TMC2209 socket; no photo set is in this repo.
- **Which Pi USB-A port is Arduino vs Monster8** — not assigned by jack. Identify by device: Arduino CDC is `arduino_device=` in `hardware.conf` (typically `/dev/ttyACM0`); Monster8 is `/dev/serial/by-id/usb-Klipper_*` (`printer.cfg` `[mcu] serial`). Either USB-A jack on the 3B works.

---

## **Part 2: Power Routing & Segregation**

### **2.1 Master Wiring Diagram (ASCII)**

```
                    [USB-C Panel Jack]
                            │
                     [IP2368 PD 100W]
                      BAT+    BAT-
                        │      │
    ┌───────────────────┴──────┴───────────────────────────────────┐
    │              CENTRAL POSITIVE (WAGO 5-port)                   │
    │   from BMS P+ ──┬── IP2368 BAT+ ──┬── [15A fuse] ── motor bus  │
    │                 │                 └── [5A fuse] ── logic bus   │
    └─────────────────┴────────────────────────────────────────────┘
                            │
    [4× Molicel P28A 4S1P]──┬── BMS B- / B+
         balance JST 5-pin   │      │
                            │   BMS P- ──────────────┐
                            │   BMS P+ ──(opt TVS)───┤
                            │                        │
    ┌───────────────────────┴────────────────────────┴───────────────┐
    │           CENTRAL NEGATIVE STAR GROUND (terminal block)         │
    │  BMS P- │ IP2368 BAT- │ Monster8 VIN- │ buck IN- │ Pi GND │
    └────────────────────────────────────────────────────────────────┘

Motor bus (after 15 A fuse):
    P+ ──► [opt 85°C thermal fuse] ──► [high-side switch] ──► Monster8 VIN+
    Monster8 VIN- ──► star ground (same net as USB GND — no FET here)
    Arduino D12 HIGH = switch on; LOW = VIN+ open

Logic bus (after 5 A fuse):
    P+ ──► Mini560 IN+ / IN- to star ground
    Mini560 OUT+ ──► Pi pin 2 (5 V) + Arduino 5 V
    Mini560 OUT- ──► Pi pin 6 (GND) + Arduino GND
    *** Pi is powered ONLY from Mini560 — not from Monster8 VMOT or USB backfeed ***

[LTC2944] across BMS P+ / P- (high-side sense), I2C to Pi pins 3/5 (I2C2)

Tier data:
    Pi ═══ USB ═══ Monster8 (Klipper MCU, 5 V logic from USB jumper only)
    Pi ═══ USB ═══ Arduino (keyboard + safety CDC @ 115200)
    Pi I2S1 M1 ──► MAX98357A ──► 8 Ω speaker (BCLK = pin 12, not pin 38)
    Pi SPI3 ──► ST7789 (CS = pin 24 / spidev3.0)
    Pi pins 3/5 (I2C2) ──► LTC2944, DRV2605L
```

### **2.2 BMS Wiring Walkthrough (Step-by-Step)**

**Parts:** 4× Molicel P28A in 4S1P holder, HiLetgo 4S 30 A BMS, IP2368, WAGO, terminal block, fuses, Mini560, Monster8, and a high-side motor switch (part not selected — [Hardware Bring-Up To-Do](Hardware%20Bring-Up%20To-Do.md)).

#### Step 1 — Cell pack to BMS (B- / B+ only)

1. Wire pack **main negative** to BMS pad **B-** with **#14 AWG**.
2. Wire pack **main positive** to BMS pad **B+** with **#14 AWG**.
3. Connect the **5-pin balance JST-XH** to cell taps:
   - Pin 1 (black) → Cell 1 negative (0 V)
   - Pin 2 (white) → Cell 1/2 junction (~3.7 V)
   - Pin 3 (yellow) → Cell 2/3 junction (~7.4 V)
   - Pin 4 (blue) → Cell 3/4 junction (~11.1 V)
   - Pin 5 (red) → Cell 4 positive (~14.8 V)
4. **Do not** connect loads to B- / B+ — only to **P-** / **P+**.

#### Step 2 — BMS output to distribution

1. BMS **P-** → terminal block **position 1** (#14 AWG) — this is the **star ground anchor**.
2. BMS **P+** → WAGO **port 1** (#14 AWG).
3. Optional: **SMBJ18A** TVS cathode to P+, anode to P- at the BMS output (see §2.5).

#### Step 3 — IP2368 charge path

1. IP2368 **BAT+** → WAGO (same positive bus as BMS P+).
2. IP2368 **BAT-** → terminal block (same star ground as BMS P-).
3. Panel-mount **USB-C** to IP2368 **USB-C pads** (short leads; strain-relief the jack).
4. When USB PD is present, the module charges the pack through the shared bus. When on battery, the module can source USB-C output (bi-directional path).

#### Step 4 — Motor fuse → high-side switch → Monster8 VIN+

1. WAGO → **15 A ATC fuse** → motor **P+** node.
2. Optional skeleton thermal fuse in series on P+ (see §2.6).
3. Motor **P+** → **high-side switch input** → switch output → Monster8 **VIN+** (#14 AWG).
4. Monster8 **VIN-** → terminal block star ground (#14 AWG). Do not insert a MOSFET in this return. USB ground is the same net, so a low-side FET is bypassed.
5. Arduino **D12** enables the switch. HIGH = motors on, LOW = motors off. Pulldown the enable input so a floating Micro reset stays off.
6. Firmware keeps D12 low until the first Pi heartbeat, and latches it low on freefall or a 3 s heartbeat gap until the Pi sends `CLEAR_FAULT`. The ISR itself is a port write; the MPU free-fall counter is about 20 ms (`FF_DUR`).

#### Step 5 — Logic fuse → Mini560 → Pi & Arduino

1. WAGO → **5 A fuse** → Mini560 **IN+** (#18 AWG).
2. Mini560 **IN-** → terminal block (#18 AWG).
3. Adjust Mini560 trim to **5.1 V** under light load before attaching Pi.
4. Mini560 **OUT+** → Pi **pin 2** (5 V) and Arduino **5 V** pin.
5. Mini560 **OUT-** → Pi **pin 6** (GND) and Arduino **GND**.
6. **Do not** power the Pi from the Monster8 buck or VMOT rail.

#### Step 6 — LTC2944 fuel gauge

1. Mount LTC2944 on the **high-side battery rail** after BMS **P+** / **P-** (sense full pack voltage).
2. I2C to Pi **pin 3 (SDA2)** and **pin 5 (SCL2)**. Those pins are I2C2, not I2C1. Address `0x64`. Confirm the `/dev/i2c-N` node on the image before trusting `telemetry.conf` ([Hardware Bring-Up To-Do](Hardware%20Bring-Up%20To-Do.md)).
3. Calibrate SOC in software: 16.8 V = 100 %, 12.0 V = 0 % (`telemetry.conf`). After one full charge/discharge cycle, optionally set `battery_full_charge_counts` / `battery_empty_charge_counts` for coulomb counting.

#### Step 7 — Monster8 logic power

1. Flash Monster8 with Klipper firmware ([makerbase-mks/MKS-Monster8](https://github.com/makerbase-mks/MKS-Monster8)).
2. Set Monster8 **5 V source jumper to USB only**. Remove the VIN-derived 5 V jumper. Both fitted will fight the Mini560 from the motor rail. STM32 logic then comes from the Pi USB port, which is fed by the Mini560, separate from VIN+.
3. Connect **shielded USB-C (Monster8) → USB-A (Pi)** for Klipper serial. That cable's ground is why the motor cut has to be on VIN+, not VIN−.

### **2.3 IP2368 Bi-Directional Power Path**

The IP2368 sits **in parallel** on the battery bus (BAT+ / BAT- tied to WAGO / star ground). It is not in series with the load. **Do not** wire USB-C → IP2368 → BMS as a series chain (older V4.9/V9 ASCII drawings looked like that).

- **Charging:** USB-C PD input → IP2368 → raises BAT+ to charge the 4S pack through the BMS.
- **On battery:** IP2368 can boost/export USB-C power from the pack (useful for bench accessories).
- **Shared bus:** BMS P+, IP2368 BAT+, fused motor rail, and fused logic rail all meet at the WAGO positive node.

### **2.4 USB-C Panel Mount**

Use a **panel-mount USB-C receptacle** screwed to the enclosure, with 6–8 in pigtails to the IP2368 module. Keep the IP2368 module fixed inside the chassis; only the jack moves with the panel. This avoids flex fatigue on PD power traces.

### **2.5 TVS Diode (Not Battery Percentage)**

A TVS diode does **not** measure state of charge. It **clamps voltage spikes** on the 14.8 V rail caused by motor back-EMF, fuse blow, or inductive switching.

- **Recommended part:** SMBJ18A or P6KE18CA across P+ and P- at the distribution node (cathode to P+, anode to P-).
- **Skeleton:** Optional — skip for first bench bring-up if budget is tight; add before mobile testing.

### **2.6 Thermal Fuse (Skeleton Prototype vs Production)**

This V5.1 skeleton is a **prototype / breadboard without** the production thermal fuse. Drivers and cells use **individual heatsinks**, not a unified bar.

- **Production (custom PCB / HAT):** unified aluminum bar across all eight TMC2209 drivers + **required** 85 °C non-resettable thermal fuse on the motor rail. Do not ship production hardware without it. V4.9/V9 still own that requirement; deferring it here does not cancel it.
- **Skeleton:** **Defer** the 85 °C series thermal fuse, or place one only on the highest-risk conductor (BMS P+ lead) if you want a belt-and-suspenders prototype.

### **2.7 Motor-rail switch (high-side on VIN+)**

Monster8 VIN−, USB ground, and Pi ground are one net once the Klipper USB cable is plugged in. A low-side FET in the VIN− lead does not stop motor current; the return takes the USB ground wire instead, and it can push that current through the Pi.

| Node | Connection |
|------|------------|
| Switch input | Motor P+ after the 15 A fuse |
| Switch output | Monster8 VIN+ |
| Monster8 VIN− | Star ground, directly |
| Enable | Arduino D12, active high, with a pulldown |

D12 HIGH means motors on. D12 LOW means VIN+ is open. Klipper **M112** is still sent on freefall, comms loss, and sensor failure, because USB logic power stays up when VIN+ opens. The switch part itself is open work: [Hardware Bring-Up To-Do](Hardware%20Bring-Up%20To-Do.md). A TC4420 plus IRLZ44N does not implement this high-side break.

---

## **Part 3: Data, Logic & Peripheral Wiring**

### **3.1 Direct Pin Keyboard (12 Physical Keys)**

Twelve tactile switches — **no 13th physical Menu key**. The system Menu overlay is invoked in software via **backtick (`)** on USB/evdev keyboards or the remote display; the protocol still defines `BRAILLATRON_KEY_MENU` (bit 12) for future use, but **pin A5 is not wired** on this skeleton.

| Button | Function | Arduino Pin |
|--------|----------|-------------|
| 1–6 | Braille dots 1–6 | D4, D5, D6, D8, D9, D10 |
| 7–8 | D-pad Up / Down | D11, A4 |
| 9–12 | Backspace, Enter, Shift/TTS, Speech | A0, A1, A2, A3 |

- Common ground bus → Arduino GND.
- `INPUT_PULLUP`, active LOW, 15 ms debounce, 40 ms chord window.
- Firmware may still define a 13th logical slot on A5 for protocol compatibility — leave **A5 unwired**.

### **3.2 MKS Monster8 V2 — Klipper MCU (Tier 2)**

#### Driver slot map (default)

| Slot | Axis / role | Motor | `run_current` (A RMS) | Notes |
|------|-------------|-------|------------------------|-------|
| 0 | X carriage | 17HS08-1004S | **0.85** | Slim NEMA 17, 1.0 A rated |
| 1 | Y paper feed | 17HS15-1504S | **1.20** | High-torque NEMA 17, 1.5 A rated |
| 2 | Emboss dot 1 | NEMA 14 | **0.80** | Row A |
| 3 | Emboss dot 2 | NEMA 14 | **0.80** | Row B |
| 4 | Emboss dot 3 | NEMA 14 | **0.80** | Row A |
| 5 | Emboss dot 4 | NEMA 14 | **0.80** | Row B |
| 6 | Emboss dot 5 | NEMA 14 | **0.80** | Row A |
| 7 | Emboss dot 6 | NEMA 14 | **0.80** | Row B |

- Install TMC2209 StepSticks in slots **0–7**; UART jumpers under each socket (no jumper photos in this repo).
- Klipper cartesian kinematics also require a `[stepper_z]` object. In `printer.cfg` that object is a **dummy** on unused EXP1 pins (PB2 / PE10 / PE11). It is **not** Monster8 slot 2 (silkscreen Z = `emboss_1`). Do **not** attach a motor to EXP1.
- **Microstepping:** 16× from `printer.cfg` (`microsteps: 16`) over UART. With the UART jumper fitted, MS1/MS2 are the driver address straps, not the microstep code.
- **Sensorless homing:** not used — optical endstops only.

#### USB & logic power

- Shielded **USB-C → USB-A** to Pi; ferrite on cable.
- Jumper: **5 V from USB** (Pi port powered by Mini560 only).
- Klipper serial: `/dev/serial/by-id/usb-Klipper_stm32f407xx_*`

#### Endstops (limits)

Wire to Monster8 endstop headers (5 V / GND / SIG):

| Sensor | Klipper object | Monster8 port (`printer.cfg`) |
|--------|----------------|-------------------------------|
| TCST2103 Y home | `stepper_y` endstop | **Y-STOP `^PA15`** |
| TCRT5000 paper edge | `stepper_x` endstop (QUERY_ENDSTOPS rail `"x"`; never `G28 X`) | **X-STOP `^PA14`** |

**PN map (do not swap):** **TCRT5000** = paper edge (reflective IR); **TCST2103** = Y home (transmissive slot).

Do **not** wire paper-edge to **E0-STOP** or **FIL_RUNOUT**. Monster8 pin names come from `klipper/printer.cfg` only. PA14 is reused as paper-edge because the carriage has no X home sensor.

**Option A:** limits live on Monster8 only. The Pi reads state via **Moonraker/Klipper API** (`query_endstops`, object status) — not Pi GPIO. Leave `gpio_paper_edge` / `gpio_y_home` empty in `telemetry.conf` unless you duplicate sensors for bench test.

### **3.3 MPU6050 & motor-rail enable (Tier 3)**

Keep MPU wiring **under 10 cm** (24–26 AWG).

| MPU6050 | Arduino Micro |
|---------|---------------|
| VCC | 5 V |
| GND | GND |
| SDA | **D2** (I2C SDA) |
| SCL | **D3** (I2C SCL) |
| INT | **D7** (PE6 / hardware interrupt **INT6**) — **active-low latched**, ISR on **FALLING**. **Not D3 / INT0** (D3 is I2C SCL). V9 active-high language and GY-521 jumpers that leave INT active-high will **miss freefall**. |
| ADO | GND (address 0x68) |

**D12:** active-high enable for the high-side switch on VIN+ (§2.7). Pulldown the input. Firmware drives the pin low in `setup()` and leaves it low until the first heartbeat.

### **3.4 I2C Bus (Pi `i2c-1`)**

| Device | Address | Pins |
|--------|---------|------|
| LTC2944 | 0x64 | SDA pin 3, SCL pin 5 |
| DRV2605L | 0x5A | same bus |

Header pins 3 and 5 are **SDA2/SCL2 (I2C2)**. Bootstrap appends overlay `i2c1` to `/boot/dietpiEnv.txt` (not `armbianEnv.txt`). That overlay name does not match this header mux. Confirm with `i2cdetect` before using `/dev/i2c-1` ([Hardware Bring-Up To-Do](Hardware%20Bring-Up%20To-Do.md)).

**Production-only / unspecified on this skeleton:** PDM MEMS mic (ICS-43432), grounded copper-tape cage, custom HAT netlist, and DRV2605L **EN** (breakout assumed strapped high; no V5.1 pin). Do not invent a HAT schematic. Skeleton STT uses the Pi 3.5 mm aux jack (Part 1 audio row).

### **3.5 I2S MAX98357A**

| MAX98357A | Orange Pi 3B |
|-----------|--------------|
| VDD | Pin 4 (5 V) or logic 5 V bus |
| GND | Pin 6 |
| LRCK (WS) | Pin 35 (GPIO3_D0, I2S1 M1 LRCK TX) |
| BCLK | Pin 12 (GPIO3_C7, I2S1 M1 SCLK TX) |
| DIN | Pin 40 (GPIO3_D1, I2S1 M1 SDO0) |
| GAIN | GND (9 dB) |
| SD | 3.3 V via 100 kΩ |
| Speaker | 8 Ω 3 W to OUT+ / OUT- |

Pin 38 is I2S1 data-in, not the bit clock. Bootstrap appends `rk3566-i2s1-overlay` to `/boot/dietpiEnv.txt`. The overlay file is not in this repo, and the onboard headphone codec already uses I2S1 mux M0. Confirm the overlay muxes M1 before soldering ([Hardware Bring-Up To-Do](Hardware%20Bring-Up%20To-Do.md)). Factory speaker test: `speaker-test -t sine -f 440 -c 1`.

### **3.6 ST7789 SPI Display**

Enable SPI3 + spidev overlay. Recommended wiring:

| ST7789 | Orange Pi pin | SPI / GPIO |
|--------|---------------|------------|
| VCC | 1 or 17 | 3.3 V |
| GND | 6 | GND |
| SCL | 23 | SPI3_CLK |
| SDA | 19 | SPI3_TXD (MOSI) |
| CS | 24 | Rockchip SPI3 CS0 (`/dev/spidev3.0`). Orange Pi silkscreen often labels this pin SPI3_CS1. Pin 26 is RES, not chip-select. |
| DC | 22 | GPIO (gpiochip4 line 9) |
| RES | 26 | GPIO (gpiochip4 line 7) |
| BLK | 3.3 V | backlight always on |

`display.conf`: `spidev=/dev/spidev3.0` (DietPi `spi3-spidev`), `gpio_chip=gpiochip4`, `gpio_dc=9`, `gpio_rst=7` (header pins 22/26). Confirm the chip node with `gpioinfo` if a kernel enumerates GPIO4 elsewhere.

---

## **Part 4: Firmware & Software Architecture**

### **4.1 Three-Tier Software Stack**

```
[Apps / braillatron-ui]
        │
        ├── Moonraker API ──► Klipper ──► Monster8 (USB serial)
        │                         ▲
        │                    G-code / M112
        │
        ├── USB CDC ──► Arduino (protocol v1 @ 115200)
        │      HEARTBEAT + TELEMETRY relay
        │
        └── I2C telemetry (LTC2944), I2S audio, SPI display
```

- **MotionService** (Pi) should emit **Klipper commands** via Moonraker's Unix socket or `~/printer_data/comms/moonraker.sock` — not Pi UART step pulses. Example mappings:
  - Line feed → scripted `G1` on Y stepper
  - Emboss dot → short pulse on mapped extruder/stepper or custom macro
  - Homing → `G28 Y` / sensor-specific `G28` config
  - Emergency → **M112** (full stop) when Arduino sends `BRAILLATRON_OP_SAFETY` / freefall
- **Pi-native UART4/UART9 TMC path is retired** under Option A.

### **4.2 Protocol v1 (Arduino ↔ Pi)**

Authoritative: `shared/protocol.h`, `shared/protocol.md`.

| Opcode | Direction | Purpose |
|--------|-----------|---------|
| `0x01` KEYBOARD_MATRIX | Arduino → Pi | Edge function keys |
| `0x02` TELEMETRY | Pi → Arduino | Battery %, temp, limit flags |
| `0x03` SAFETY | Arduino → Pi | Freefall, comms loss, sensor failure |
| `0x04` HEARTBEAT | Pi → Arduino | 500 ms liveness (`braillatron-ui`) |
| `0x06` CHORD | Arduino → Pi | Braille dot mask |
| `0x07` CLEAR_FAULT | Pi → Arduino | Drop latched freefall / comms-loss and re-apply the rail |

**USB heartbeat:** `braillatron-ui` opens the Arduino CDC device and sends `HEARTBEAT` on the configured interval. The rail stays off until the first heartbeat. If heartbeats then stop for more than 3 s, the Arduino cuts the rail and latches that off until `CLEAR_FAULT`. A later heartbeat does not turn it back on.

**TELEMETRY relay:** `braillatron-ui` reads `/run/braillatron/telemetry.json` (from `braillatron-sentinel`) and sends `TELEMETRY` frames to Arduino every 500 ms with battery and limit flags.

### **4.3 Watchdog Keyboard & Debounce**

Non-blocking `millis()` state machine — never `delay()` in the main loop (MPU INT must stay sub-10 ms).

### **4.4 MPU6050 Freefall → Klipper M112**

On INT6 (D7 / PE6) **falling** edge (MPU INT is **active-low latched**; firmware `INT_PIN_CFG = 0xA0`):

1. Arduino **ISR** pulls D12 LOW, which opens the high-side switch (port write only — **no serial in the ISR**).
2. The **main loop** sees the pending latch and transmits `BRAILLATRON_OP_SAFETY` with `BRAILLATRON_FAULT_FREEFALL` (`braillatron_app.cpp`).
3. Pi handler issues **Klipper emergency stop** via Moonraker and blocks MotionGate. The same stop is sent for comms loss and sensor failure.

### **4.5 DietPi / gpiod Heartbeat (development)**

```bash
sudo apt install -y gpiod
gpioinfo
# Example bench pulse (adjust chip/line to your heartbeat GPIO if used):
gpioset gpiochip3 22=1; sleep 0.1; gpioset gpiochip3 22=0
```

Production heartbeat uses USB CDC from `braillatron-ui`, not bit-banged GPIO.

### **4.6 Klipper MCU Verification**

```bash
ls /dev/serial/by-id/*
# Expect: usb-Klipper_stm32f407xx_...
```

Copy path into `printer.cfg` `[mcu]` section.

### **4.7 Homing Status**

`braillatron-sentinel` / `homing_service.cpp` writes `/run/braillatron/homing.status` on boot when `motion_enabled=true`. With Klipper, homing completion should also be polled from Moonraker printer objects.

### **4.8 Factory Test Mode**

Factory Test is a standalone app on the home screen (`apps/factory_test_app.cpp`). Access is controlled in `ui.conf`:

```ini
dev_mode=false      # false on deploy images — PIN required
factory_pin=1234    # four-digit PIN when dev_mode=false; skipped when dev_mode=true
```

When `dev_mode=true` (bench builds), the app opens directly. On production images (`dev_mode=false`), enter the PIN digit-by-digit with Braille dots or the D-pad, then **Enter**.

**Test menu** (each item runs on **Enter**; motor tests use Klipper `STEPPER_BUZZ` — brief bursts only):

| Test | What it exercises |
|------|-------------------|
| Motor X carriage | `stepper_x` buzz |
| Motor Y feed | `stepper_y` buzz |
| Motor emboss dot 1–6 | `emboss_1` … `emboss_6` buzz |
| Speaker 440 hertz sine | `speaker-test -f 440` |
| Arduino button matrix | Prompts for physical key presses on the co-processor |
| Battery percent | LTC2944 / `/run/braillatron/telemetry.json` |
| Charging state | USB PD / IP2368 charge path |
| LTC2944 temperature | Fuel-gauge die temp |
| Paper endstops (Klipper) | Moonraker `query_endstops` (`y_home`, `paper_edge`) |
| DRV2605L haptic pulse | Output Hub haptic boundary effect |
| Motion gate status | Reports `MotionGate::block_reason()` when blocked |

Motor and emboss tests are **skipped** when MotionGate is blocked (freefall, comms loss, battery critical, etc.) except for status, telemetry, speaker, and button tests. Charging test: apply USB PD and confirm IP2368 charge LED plus rising cell voltage on LTC2944.

### **4.9 Configuration Flags**

`ui.conf` (deploy to `/etc/braillatron/`):

```ini
dev_mode=false
factory_pin=1234
```

`hardware.conf` (deploy to `/etc/braillatron/`):

```
board_profile=skeleton_v5
allow_missing_arduino=false
motion_enabled=true
```

`evdev_enabled=true` remains for USB keyboard bench input.

`matrix_map.conf` is an **obsolete leftover** from the retired 4×4 key-matrix era (identity remap). The daemon still loads it; do not delete until that path is removed. See the file header.

---

## Part 5: Klipper configuration (authoritative)

**Do not copy pin excerpts from older docs.** The repo ships the canonical Monster8 config:

[`klipper/printer.cfg`](../klipper/printer.cfg) — 8 **real** steppers (X, Y, emboss dots 1–6), plus a **dummy** cartesian `[stepper_z]` on unused EXP1 pins (required by Klipper cartesian kinematics — do **not** attach a motor). TMC2209 UART, 16× microsteps, Y-STOP `^PA15`, paper-edge on **X-STOP `^PA14`**.

1. Flash Monster8 with Klipper firmware ([makerbase-mks/MKS-Monster8](https://github.com/makerbase-mks/MKS-Monster8)).
2. Copy `klipper/printer.cfg` to `~/printer_data/config/printer.cfg` on the Pi.
3. Set `[mcu] serial` to your USB ID: `ls /dev/serial/by-id/usb-Klipper_*`
4. Verify endstop pin names against your Monster8 V2 Klipper build and physical wiring before first homing.

> **Stale excerpt warning:** Earlier guides listed example `step_pin` / `dir_pin` values that may not match your Klipper MCU build. Always edit the repo `printer.cfg` in place and diff against `klipper3d/klipper` board pin definitions for MKS Monster8 V2.

---

## Related documentation

- [Master Software Architecture V9](Master%20Software%20Architecture%20V9.md) — product architecture, applications, protocol, implementation status
- [Master Architecture V4.9](Master%20Architecture%20V4.9.md) — power rails, PCB lifecycle; **§4 UART daisy chain retired under Option A**
- [Pi SD Image Software Build Guide](Pi%20SD%20Image%20Software%20Build%20Guide.md) — DietPi image, overlayfs, deploy
- `shared/protocol.h` / `shared/protocol.md` — Arduino ↔ Pi frame protocol
