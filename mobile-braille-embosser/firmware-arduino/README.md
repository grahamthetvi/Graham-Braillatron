# Arduino Micro firmware

Real-time co-processor for the Braillatron: 13-key direct-pin keyboard scan, 40 ms braille chord assembly, MPU6050 freefall interlock, and USB CDC serial to the Pi at 115200 baud.

**Board:** Arduino Micro (`arduino:avr:micro`), profile `skeleton_v5` — see [src/pins.h](src/pins.h).

**Protocol:** Wire format and opcodes live in [../shared/protocol.h](../shared/protocol.h). The files `src/protocol.h` and `src/protocol.c` are symlinks into `shared/`; edit protocol definitions there only.

## Prerequisites

- [arduino-cli](https://arduino.github.io/arduino-cli/)
- Linux USB access: membership in the `dialout` group (or equivalent) for upload

## One-time setup

```bash
arduino-cli core update-index
arduino-cli core install arduino:avr
make check-deps
```

The Wire library ships with the AVR core (used by the MPU6050 driver).

## Compile

From this directory:

```bash
make compile
```

Or from the repo root:

```bash
make -C mobile-braille-embosser/firmware-arduino compile
```

## Flash

Connect the Micro over USB. When the Pi also uses a serial device, the co-processor often appears as `/dev/ttyACM1` while the Pi is `/dev/ttyACM0` — adjust `PORT` as needed.

```bash
make upload PORT=/dev/ttyACM1
```

If upload fails, press the reset button on the Micro once and retry immediately.

## Pi-side pairing

Set `arduino_device=` in [daemon-dietpi/config/hardware.conf](../daemon-dietpi/config/hardware.conf) to match the device node the Micro exposes on the Pi (typically `/dev/ttyACM0`).
