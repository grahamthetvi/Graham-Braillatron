# Two products, one roof — rough draft

**Status:** brainstorm for the university build. This note maps the split. It does not replace [Master Software Architecture V9](Master%20Software%20Architecture%20V9.md), [Master Architecture V4.9](Master%20Architecture%20V4.9.md), or the [Skeleton Prototype V5.1 Build Guide](Skeleton%20Prototype%20V5.1%20Build%20Guide.md). Those remain the personal-computer spec. The embosser motor section below is the working choice: a FYSETC Spider (or a comparable eight-socket board such as the BTT Octopus) plus one ESP32-S3. A WROOM-32 is not a second module. There is no firmware port onto the Spider yet.

Commit `54a3621` replaced `firmware-arduino/firmware-arduino.ino` (the Arduino Micro entry point, `braillatron_setup` / `braillatron_loop`) with an ESP32-S3 BLE keyboard bridge: a phone writes Nordic UART, and the chip types those bytes as USB HID. That bridge is a third idea (a phone typing into a host). It now lives in `firmware-keyboard-bridge/`. It is not the embosser, and it is not the motor-rail interlock. `firmware-arduino/firmware-arduino.ino` again calls `braillatron_setup` / `braillatron_loop`. The safety sources under `firmware-arduino/src/` are unchanged.

A builder picks one product:

| Product | What it is | Brain |
| --- | --- | --- |
| **Braillatron** | Personal computer. The repo as it stands: keyboard, apps, speech, display, and a head that can emboss on the same machine. | Orange Pi 3B, DietPi, Klipper host, Monster8, Arduino Micro safety co-processor |
| **Braillatron embosser** | A printer. Its only job is to accept a `.brf` and punch it. Cheaper parts, because the apps and the notetaker stay behind. | FYSETC Spider (STM32F446, eight driver sockets) for the steps, plus one ESP32-S3 for Wi-Fi and USB |

Both products share the mechanism and the print contract. Each product has its own computer and its own code that turns a cell into steps.

---

## The roof

The roof is small on purpose. It is the page geometry, the meaning of a BRF file, and the states a sender can rely on. It is not one binary, and it is not the Monster8 pin map.

### Page geometry

These numbers already live in `daemon-dietpi/src/kinematics/motion_constants.h` and are what `MotionService::emboss_brf` prints. Both brains honor them.

| Quantity | Value | Where it is fixed today |
| --- | --- | --- |
| Cell pitch | 6.0 mm, Marburg Medium | `CELL_PITCH_MM` |
| Dot pitch / Row B stagger | 2.5 mm along the carriage | `ROW_B_X_OFFSET_MM` |
| Line advance | 10 mm | `LINE_ADVANCE_MM` |
| Page length | 33 lines, then a form feed starts a fresh page | `MotionService::emboss_brf` |
| Row A punches | Dots 1, 3, 5, fired at the current carriage position | `ROW_A_DOT_MASK` |
| Row B punches | Dots 2, 4, 6, fired 2.5 mm later | `ROW_B_DOT_MASK` |
| Axes | X carriage, Y tractor, six punch actuators | `klipper/printer.cfg` slots 0–7 on the personal computer |

Logical actuator names are part of the roof: `emboss_1` … `emboss_6` are dots 1 … 6. The Monster8 step pins in `printer.cfg` belong to the personal-computer brain. The embosser firmware will have its own pin table for whatever cheaper driver board is fitted.

Carriage and tractor gearing in `motion_constants.h` (GT2 20-tooth X, 20 mm per Y motor revolution, 16× microstepping) describe the personal-computer drive. The embosser mechanism should hit the same millimetres on the page. If its pulleys differ, that brain converts millimetres to steps locally. The contract stays in millimetres and cells, not in Monster8 microsteps.

### The print job

A job is North American Braille ASCII (a `.brf` file). The personal computer already accepts that file on a USB-serial cable:

- `daemon-dietpi/src/documents/brf_cable.h` — `BrfCableParser`
- 115200 8N1
- Optional first line: `BRF1 filename.brf`
- The job ends on a form feed, which is what Graham Braille Editor’s generic text embosser writes, or after 1.5 s of silence
- Cells are decoded by `tokenize_brf` / `brf_ascii_to_dot_mask` in `brf_format.h` (the 64-entry North American table). Unmapped characters are dropped. A space is a blank cell and still takes a cell width.

On the personal computer that cable path stores the file in the library and embosses only when configured (`brf_cable_emboss` is `always` or the user confirms). On the embosser the same bytes go straight to the head. One grammar, two reactions.

Proposed job states, same words on both products:

| State | Meaning |
| --- | --- |
| `idle` | Ready for a job |
| `receiving` | Bytes are arriving and the job has not closed |
| `queued` | A complete `.brf` is waiting for the head |
| `printing` | The head is on that job |
| `done` | The job finished and the head is back at rest |
| `fault` | Stopped. Reason is one of `safety`, `paper`, `motion`, `overflow` |

A sender learns the state on the same link that carried the file. A single status line is enough for the first cut (`printing`, `done`, `fault paper`). A network-printer protocol can sit on top of this later. The personal computer, a laptop, and Graham Braille Editor are all just senders of those bytes.

### Safety rule

A drop cuts motor power, and the cut does not depend on the radio or on Linux. On the personal computer this is the Arduino Micro: MPU6050 freefall on D7 drives D12 low and opens the high-side switch on Monster8 VIN+. On the embosser the same rule is a pin that fails off, held by a watchdog, owned by the chip that generates steps. Wi-Fi being up is not what keeps the motors alive.

---

## What each brain owns

```
                    shared roof
        geometry + BRF job + job states + drop cuts power
                    /                    \
     Braillatron (personal computer)      Braillatron embosser
     Orange Pi + Klipper + Monster8       ESP32-class MCU
     Arduino Micro safety co-processor    cheaper stepper board
     apps, speech, display, keyboard      receive BRF, punch, report state
```

### Personal computer — already in the tree

| Piece | Path | Stays |
| --- | --- | --- |
| UI, apps, speech, display | `daemon-dietpi/src/ui/`, `connect/` | This product only |
| BRF editor storage | `documents/brf_store.*` | This product only |
| Cable receive into the library | `documents/brf_cable.*`, `UiApp::handle_brf_cable_jobs` | This product’s reaction to a job |
| BRF codec and emboss pipeline | `documents/brf_format.*`, `kinematics/`, `motion/motion_service.cpp` (`emboss_brf`) | Pi implementation of the roof |
| Klipper host and motor board | `klipper/printer.cfg`, Moonraker bridge | This brain only |
| Safety co-processor | `firmware-arduino/`, `shared/protocol.h` | This brain only |
| DietPi image | `deploy/` | This product’s image |

`MotionService::emboss_brf` is the personal computer’s implementation of the pipeline: tokenize, strike Row A at the cell, defer Row B by 2.5 mm, return the carriage and feed 10 mm on a newline, feed out to the next 33-line page on a form feed. That C++ uses `std::function` and talks to Moonraker. It stays on the Pi.

### Embosser — a second implementation

The embosser firmware receives a job and generates steps. It does not run DietPi, Klipper, Moonraker, the app list, or the Arduino co-processor protocol.

Rough pipeline, matching `emboss_brf`:

1. Accept the cable byte stream over USB serial, or the same byte stream over TCP once Wi-Fi is up.
2. Close the job on form feed or on the idle gap.
3. Stream it. Decode a line, punch the line, then keep the rest. The cable parser’s 2 MB cap fits the Pi. An ESP32-class part has much less internal RAM, so the embosser spools to external flash or prints as bytes arrive. It does not buffer a whole large file in SRAM.
4. For each cell, fire dots 1, 3, 5, then dots 2, 4, 6 after 2.5 mm of carriage travel.
5. Newline: finish the deferred Row B strikes, return X to the margin, advance Y 10 mm.
6. Form feed: advance to the next 33-line page.
7. Publish `printing`, then `done` or `fault`.

Wireless and wired are two sockets into that same parser. Wired is USB serial at 115200 8N1. Wireless is a TCP port that carries the identical bytes, after the board has joined a network. Finding the board (mDNS name, or a setup access point) is open; see below.

Step pulses stay on the Spider’s STM32. The ESP32-S3 only receives the job and reports status. The STM32 cuts motor power if its own loop stalls, and it also cuts power if the ESP32 stops sending a heartbeat. That split is the embosser’s version of the Arduino heartbeat.

### What a cheaper bill of materials actually changes

The six punch motors, the carriage, the tractor, the paper sensors, and the battery are the cost of the mechanism. They sit under the roof and show up on both BOMs.

The personal-computer brain adds the Orange Pi 3B, the Monster8, the Arduino Micro, the Perkins keyboard, the display, and the speaker.

### Embosser motor section

The Monster8 stays on the personal computer. The embosser does not use one, and it does not use a hand-wired carrier of eight loose driver modules.

Working choice: a **FYSETC Spider** (STM32F446, eight driver sockets) plus **one ESP32-S3**. A BTT Octopus is the same kind of board if a Spider is hard to get. An ESP32-WROOM-32 is not added beside the S3. The Spider already has the step computer, and the S3 already has Wi-Fi and native USB. A WROOM-32 would be a third computer doing the same radio job.

What each chip owns:

| Piece | Chip | Job |
| --- | --- | --- |
| Driver sockets, step pulses, motor enable | Spider’s STM32F446 | Turn each cell into steps. Cut the motor rail if its loop stalls or the ESP32 heartbeat stops. |
| Wi-Fi and USB | ESP32-S3 | Accept the `.brf` byte stream, hand cells to the Spider, send `BRFSTAT` back. |

The eight TMC2209s seat in the sockets the Spider already has. Motor phase wires land on the Spider’s motor terminals, the same kind of hookup as the Monster8. Nobody wires STEP, DIR, and ENN by hand. Analog’s TMC2209 datasheet (rev 1.08) straps 16 microsteps with MS1 and MS2 both tied to VIO, matching the personal computer’s `microsteps: 16`.

Same motors and currents as `klipper/printer.cfg`: X `17HS08-1004S` at 0.85 A run, Y `17HS15-1504S` at 1.20 A run, six NEMA14 punches at 0.80 A run. A TMC2209 is rated 2 A RMS. Punch nameplate current is still unchecked; see the hardware bring-up list.

VMOT still passes through the high-side switch that fails off. The Spider’s STM32 owns that pin. Paper home and paper edge land on the Spider. The link between the ESP32-S3 and the Spider is a short UART: cells one way, `BRFSTAT` the other.

No prices in this draft. The embosser drops the Orange Pi, the Monster8, and the Arduino Micro. It keeps an eight-socket printer board because that is what makes the motors a plug-in job.

---

## How the tree should grow

The first slice below is in the tree. The shape it is growing into:

```
mobile-braille-embosser/
├── shared/
│   ├── protocol.h            personal-computer co-processor link (unchanged)
│   ├── print_contract.h      C header both brains include
│   └── print_contract.md     short form of that header
├── daemon-dietpi/            personal-computer brain (unchanged home)
├── firmware-arduino/         personal-computer safety co-processor
├── firmware-keyboard-bridge/ phone-to-USB-HID typing bridge (not either product's brain)
├── klipper/                  personal-computer motor board
├── firmware-embosser/        embosser pipeline, host-tested; Spider steps, ESP32-S3 receives
├── deploy/                   personal-computer image
└── specs/
    ├── Master Software Architecture V9.md
    └── Two Product Draft.md  this note
```

`print_contract.h` should be plain C so an ESP32 toolchain and the Pi daemon can include it. Contents, and nothing else:

- The millimetre geometry (cell, stagger, line, page length).
- Row A / Row B masks and the logical names `emboss_1` … `emboss_6`.
- The 64-byte North American BRF table copied from `brf_format.cpp`.
- Job-state and fault enums.
- The wire markers: `BRF1 `, form feed, 115200, idle gap 1500 ms.
- The status line: `BRFSTAT <state>` or `BRFSTAT fault <reason>`.

The Pi files keep their behavior. `motion_constants.h` static-asserts against `print_contract.h` so the two cannot drift. The embosser firmware includes the header. Its host pipeline schedules strikes; the step generator is still unwritten. `EmbossScheduler` is not ported line by line.

The personal computer can send a document to a standalone embosser by writing the same bytes `BrfCableParser` already accepts, to a USB serial port or to that TCP port. Local embossing stays `emboss_brf`. Two destinations, one file.

---

## First build slice

Landed in the tree. The motor section is a FYSETC Spider plus one ESP32-S3. There is no Spider firmware and no ESP-IDF port yet.

1. `shared/print_contract.h` is the roof in C: micrometre geometry, row masks, logical names `emboss_1` … `emboss_6`, the 64-byte North American BRF table, job states, fault reasons, and the wire markers (`BRF1 `, form feed, 115200, 1500 ms). `braillatron_print_status_line` writes `BRFSTAT <state>` or `BRFSTAT fault <reason>`. `shared/print_contract.md` points at the header as the source of truth.
2. The Pi daemon includes that header. `motion_constants.h` static-asserts the millimetre constants and row masks. `MotionService::emboss_brf` static-asserts the 33-line page. `brf_format.cpp` uses the shared table. `BrfCableParser::kIdleCompleteMs` stays 1500 and is checked against the contract. The embosser does not take the Pi's 2 MB job buffer. `make brf-test` still covers the codec.
3. `firmware-embosser/` streams BRF on the host: optional `BRF1` header, Row A at the cell, Row B at that X plus 2.5 mm during the cell advance, newline flush, 33-line form feed, and `BRFSTAT` lines. Motor output is function pointers. `emboss_pipeline_safety_cut` drops enable and enters `fault safety`. `make check` at the repo root runs this test after the daemon check. `queued` is in the contract for a later spool; this pipeline does not emit it.
4. DietPi, Klipper, and the Arduino Micro safety firmware stay on the personal-computer path. The ESP32-S3 BLE HID sketch is `firmware-keyboard-bridge/` and is not on the AVR CI job.

---

## Open questions

The motor section is chosen: a FYSETC Spider for the steps, one ESP32-S3 for Wi-Fi and USB. A WROOM-32 is not on this board. These are the decisions that still block the firmware port.

1. **Spider UART.** The short link from the ESP32-S3 to the Spider: cell bytes one way, `BRFSTAT` the other, plus a heartbeat the STM32 requires before it will enable the motors.
2. **Octopus as the alternate.** Use a BTT Octopus only when a Spider cannot be sourced. The firmware should talk to the driver sockets, not to one board’s silkscreen names, so the swap stays possible.
3. **Wi-Fi bring-up on a headless printer.** Join a network with credentials sent over the USB serial link, or boot an access point for first setup?
4. **Job size.** Stream line by line, or spool a whole `.brf` to a flash chip first so a dropped Wi-Fi session can resume?
5. **Local keys.** Headless only, or a feed key and a cancel key on the embosser?
6. **Sensors.** Same TCST2103 home and TCRT5000 paper-edge parts, on the cheap controller’s GPIO?
7. **Battery and the drop switch.** Same pack as the personal computer, with this chip owning the high-side enable?
8. **Who writes the step generator.** The university team can own the Spider firmware. The frozen input is `print_contract.h` plus this note’s pipeline. The ESP32-S3 only receives the job and forwards cells. Step timing on the STM32 is what they settle on the bench.
