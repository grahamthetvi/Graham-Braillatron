# Hardware Bring-Up To-Do

Work that still needs the physical Braillatron. Wiring that the software and the build guide already agree on is not repeated here. Do not install the 15 A motor fuse until section 1 is checked on the bench.

The Pi now reads Klipper endstop strings (`TRIGGERED` / `open`). The motor rail stays off until the first USB heartbeat, and a lost heartbeat stays latched until `CLEAR_FAULT` (`0x07`). Critical safety frames also send Klipper emergency stop. Those code changes do not replace the checks below.

## 1. Motor power cut

The old low-side IRLZ44N on Monster8 VIN− does not open the motor circuit. Monster8 ground, USB ground, and the Pi ground are the same net, so motor current returns through the USB cable. Do not build that circuit.

- [ ] Choose a high-side switch in the VIN+ lead, after the 15 A fuse. It must hold the 4S pack (16.8 V max) and the motor current. A TC4420 cannot turn a P-channel FET off on that rail: its output only swings 0–5 V. No part number is selected in this repo.
- [ ] Arduino D12 is active-high enable: HIGH = motors on, LOW = motors off. Match the switch to that polarity.
- [ ] Pulldown on the enable input. Until `setup()` runs, D12 is high-impedance and a floating input can turn the rail on through the bootloader.
- [ ] Monster8 VIN− goes straight to star ground. No MOSFET in that wire.
- [ ] Monster8 5 V jumper: USB only. Leave the VIN-derived 5 V jumper empty so the motor rail cannot backfeed the buck.
- [ ] With the USB cable plugged in, command the rail off and confirm VIN+ actually falls. Unplugging USB to “make a low-side FET work” lets the Monster8 ground float up toward pack voltage.
- [ ] Arduino Micro USB VBUS backfeeds 5 V through the power MOSFET body diode. Power the Pi and the Micro from the Mini560, and do not let USB VBUS be a second 5 V source.

## 2. Measure before the first emboss

Software cannot know these. Punches stay backed off the paper until the numbers are written down.

- [ ] Y millimetres per motor revolution, with the real sprocket and any gear. `printer.cfg` uses 20 mm/rev only if the circumference is 127 mm and the reduction is 6.35. Confirm +Y (dir `!PE4`) feeds the way you want before homing.
- [ ] X millimetres per revolution. 40 mm/rev assumes a GT2 belt, 2 mm pitch, 20-tooth pulley. Then check that a commanded 6.0 mm cell and a 2.5 mm column land on a gauge. `position_max` on X is 200 mm, about 33 cells.
- [ ] Which physical row is downstream of +X. The code punches row A (dots 1, 3, 5), moves +X, then punches row B (dots 2, 4, 6).
- [ ] Crank: degrees from clear to a full dimple, pin travel, and hard stops for each NEMA 14. Set `rotation_distance` and `emboss_stroke_mm` together. Today the macro moves 2.0 mm on `rotation_distance` 40, about 18° of the motor, and each strike does `SET_POSITION=0` with no punch home. A mid-stroke power cut is forgotten. `kinematics.conf` crank fields are not applied to the dot spacing.
- [ ] NEMA 14 nameplate current. 0.80 A per punch has no motor behind it.
- [ ] `QUERY_ENDSTOPS` idle and blocked for the TCST2103 on Y-STOP `PA15` and the TCRT5000 on X-STOP `PA14`. `printer.cfg` uses `^` (trigger when high). The Makerbase sample uses `!` (trigger when low). A sensor that never trips will run Y out to 500 mm. Do not run a bare `G28`: that homes X into the paper sensor and wiggles the dummy Z pins on EXP1. Software homes with `G28 Y` only.
- [ ] Pull TMC DIAG-to-endstop jumpers if they are fitted. Left in place, DIAG fights the optical sensors.
- [ ] StealthChop is forced on (`stealthchop_threshold: 999999`) on X, Y, and all six punches. Under card stock, try SpreadCycle (`0`) if the tractor skips or dots stay shallow. Hold current stays on after each punch, and the skeleton has no thermal fuse.

## 3. Header wiring to confirm on the image

- [ ] MAX98357A bit clock is header pin 12 (GPIO3_C7, I2S1 M1 SCLK). Pin 35 is word clock. Pin 40 is data out. Pin 38 is I2S data in, not the bit clock. Bootstrap appends `rk3566-i2s1-overlay` to `/boot/dietpiEnv.txt`. That overlay is not in this repo. The onboard headphone codec already uses I2S1 mux M0, so confirm the overlay before soldering the amp.
- [ ] Pins 3 and 5 are SDA2/SCL2 (I2C2). Bootstrap appends overlay `i2c1`, which does not match that mux. Run `i2cdetect -l` and `i2cdetect -y N`, then point `telemetry.conf` at the bus that shows `0x64` (LTC2944) and `0x5A` (DRV2605L). Pull-ups must be to 3.3 V. A 5 V pull-up on those pins damages the Pi.
- [ ] Display chip-select is physical pin 24, which is `/dev/spidev3.0`. Orange Pi’s silkscreen often calls that pin SPI3_CS1; Rockchip calls it CS0. Pin 26 is the display reset.
- [ ] LTC2944 sense resistor is not documented. Leave coulomb-count calibration at 0 until the resistor value is known. Voltage-only SOC will not trip the 20% / 5% policy until it is marked trusted.
- [ ] DRV2605L: VCC rail, I2C pull-ups, EN pin, and the 10 mm LRA rated voltage. EN is not in the skeleton pinout.
- [ ] MPU6050: use a regulated 3.3 V module. A bare chip on Arduino 5 V is outside its supply, and INT is not 5 V tolerant. Firmware expects active-low latched INT on D7. `FF_THR` 0x30 and `FF_DUR` 0x14 are about 96 mg for 20 ms. An open INT wire does not false-trip, and it also never trips.
- [ ] IP2368: 4S cell count, charge-current cap, and whether the panel USB-C port may source power while on battery. It sits in parallel on the pack bus.
- [ ] Confirm the BMS is common-port P+/P−. There is no precharge path for the Monster8 bulk capacitors.
- [ ] Set the Mini560 to 5.1 V under light load before the Pi is attached.

## 4. Motion behavior that stays open until the bench numbers exist

- [ ] After any rail cut, re-home with `G28 Y` only and jog the carriage to a mark before embossing. Recovery sends `CLEAR_FAULT` and Klipper `firmware_restart`, which runs `SET_KINEMATIC_POSITION` and marks X, Y, and Z homed at 0. There is no carriage home sensor. `CLEAR_FAULT` has no acknowledgement: the Pi unblocks motion when the serial write succeeds, even if the Arduino kept D12 off.
- [ ] The first strike of a session punches where the carriage already is. A restored `coords.json` X is not a measured position after power loss.
- [ ] App-switch page separate always aims at 33 lines. It does not keep the distance it just traveled, and Y cannot move below 0 immediately after homing, so the reverse pass can fail while the line index still changes.
- [ ] Dot 1’s plug is Monster8 slot 2 (silkscreen Z). Slots 3–7 are dots 2–6. Do not put a motor on EXP1; that header is the dummy cartesian Z.

## 5. Mechanical parts this repository does not specify

NEMA 14 part number, coil pairing, GT2 belt, tractor sprocket, crank and linkage, chassis, keyboard plate, embossing head, Speech-key seat, and Cherry MX color. The electrical harness can be built from the V5.1 guide. The mechanism cannot.

## 6. Not this skeleton

- [ ] Production still requires the 85 °C thermal fuse and a shared heatsink across the eight drivers. The skeleton defers the fuse.
- [ ] PDM microphone and the custom HAT netlist are production-only.
- [ ] There is no A/B update path. Field updates are `deploy/install.sh` or a new image.
