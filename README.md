# Graham Braillatron

Portable smart braille notetaker and embosser: Orange Pi 3B UI daemons, Arduino Micro safety co-processor, and DietPi deployment tooling.

All software lives under [`mobile-braille-embosser/`](mobile-braille-embosser/). Start with the [developer guide](mobile-braille-embosser/README.md) to run the UI on a Linux PC with a USB keyboard — no Pi or Arduino required.

## Repository layout

```
Graham Braillatron/
├── mobile-braille-embosser/
│   ├── README.md              Developer setup (bench keyboard, build, run)
│   ├── daemon-dietpi/         Pi UI, motion, keyboard, and connect daemons
│   ├── firmware-arduino/      Arduino Micro co-processor firmware
│   ├── shared/                Serial protocol (single source of truth)
│   ├── deploy/                DietPi bootstrap, systemd units, SD card prep
│   └── specs/                 Architecture and Pi SD image build guide
└── LICENSE                    MIT
```

## Quick links

| Topic | Document |
| --- | --- |
| Dev setup (no hardware) | [mobile-braille-embosser/README.md](mobile-braille-embosser/README.md) |
| Pi SD image and deployment | [Pi SD Image Software Build Guide](mobile-braille-embosser/specs/Pi%20SD%20Image%20Software%20Build%20Guide.md) |
| Wi‑Fi and network setup (Pi) | [Pi SD Image Guide — Wi‑Fi and network connectivity](mobile-braille-embosser/specs/Pi%20SD%20Image%20Software%20Build%20Guide.md#wi-fi-and-network-connectivity) |
| connectd + app bring-up checklist | [Connectivity Follow-Up Checklist](mobile-braille-embosser/specs/Connectivity%20Follow-Up%20Checklist.md) |
| Software architecture | [Master Software Architecture V9](mobile-braille-embosser/specs/Master%20Software%20Architecture%20V9.md) |
| Serial protocol | [shared/protocol.md](mobile-braille-embosser/shared/protocol.md) |
| Arduino firmware build | [firmware-arduino/README.md](mobile-braille-embosser/firmware-arduino/README.md) |

## CI

Push and pull requests run `make check` in `daemon-dietpi/` (host self-tests) and compile the Arduino firmware with `arduino-cli`. See [.github/workflows/ci.yml](.github/workflows/ci.yml).
