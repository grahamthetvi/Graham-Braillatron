# Mobile Braille Embosser (Braillatron)

Repository overview: [../README.md](../README.md)

Software for the Braillatron portable braille embosser: Pi-side UI daemon, motion/telemetry services, and Arduino co-processor firmware.

This guide gets you from a fresh clone to a working UI on a Linux dev machine using **only a standard USB keyboard** — no Orange Pi, Arduino, or mechanical key matrix required.

## What you can try without hardware

| Feature | Works on a dev PC? |
| --- | --- |
| Focus navigation (home screen apps) | Yes |
| Global menu overlay | Yes |
| Braille chord timing (40 ms window) | Yes (`make host-chord-test`) |
| Braille dots → letters | Needs liblouis (see below) |
| Motion / embossing | No (`motion_enabled=false` in skeleton config) |
| TTS / braille display / STT | Stub backends log to stderr unless you build with `BRAILLATRON_A11Y=1` |

All UI feedback is printed to the terminal as `[ui]`, `[tts]`, and `[braille]` lines. You do not need speakers or a refreshable display to develop.

## Prerequisites

- Linux with `g++` (C++17), `make`, and kernel headers (`linux/input-event-codes.h` — usually in `kernel-headers` or `linux-libc-dev`)
- A USB keyboard with a standard QWERTY layout (the bench map uses the **F / D / S** and **J / K / L** home rows for braille dots)
- Read access to `/dev/input/event*` (typically membership in the `input` group, or run from a graphical session on Fedora/Ubuntu)

Optional, for braille-to-text translation on the host:

- `liblouis-devel` (Fedora) or `liblouis-dev` (Debian) — then rebuild with accessibility backends (see **Braille text input** below)

## Build

```bash
cd mobile-braille-embosser/daemon-dietpi
make braillatron-ui
```

This produces `braillatron-ui` with stub accessibility backends. Other useful targets:

```bash
make check                                              # build + run all host self-tests
make host-chord-test && ./braillatron-host-chord-test   # chord window logic, no keyboard
make motion-test && ./braillatron-motion-test           # kinematics math only
```

Optional liblouis self-test (requires `liblouis-dev` / `liblouis-devel`):

```bash
make check-liblouis
```

## Enable USB keyboard bench input

The built-in key matrix and Arduino link are optional when `allow_missing_arduino=true` (the default in `config/hardware.conf`). To drive the UI from your USB keyboard, turn on evdev bench mode.

**Option A — use the bench preset:**

```bash
cp config/keyboard-bench.conf config/keyboard.conf
```

**Option B — edit `config/keyboard.conf` manually:**

```ini
evdev_enabled=true
evdev_grab=false
```

Set `evdev_grab=true` only if you want the Pi-style behavior where Braillatron exclusively owns the keyboard (fine on a dedicated test device; awkward on your daily desktop).

## Run

From `daemon-dietpi/`:

```bash
BRAILLATRON_CONFIG=config ./braillatron-ui
```

You should see startup status, missing-device notices (expected without hardware), and:

```
keyboard: evdev listening on /dev/input/eventN (bench mode)
braillatron-ui profile=skeleton_v4
```

Press **Ctrl+C** to quit.

### Run from the project root

```bash
cd mobile-braille-embosser/daemon-dietpi
BRAILLATRON_CONFIG="$(pwd)/config" ./braillatron-ui
```

## Default key map

Mappings live in `daemon-dietpi/config/evdev_map.conf`. Defaults emulate the physical Perkins layout:

| Key | Braillatron action |
| --- | --- |
| **↑ / ↓** | Move focus (or move in the menu overlay when open) |
| **Enter** | Activate focused app / menu item |
| **Backspace** | Delete typed text / back out of menu |
| **F7** | Pause / resume speech (press and release) |
| **F8** (hold) | Push-to-talk dictation gate |
| **F9** | Toggle global menu overlay |
| **F** | Braille dot 1 |
| **D** | Braille dot 2 |
| **S** | Braille dot 3 |
| **J** | Braille dot 4 |
| **K** | Braille dot 5 |
| **L** | Braille dot 6 |

Braille letters are **chords**: press the dot keys together within **40 ms**, then release. Examples (Grade 1 / Perkins):

| Letter | Dots | Keys |
| --- | --- | --- |
| `a` | 1 | **F** |
| `b` | 1-2 | **F** + **D** |
| `c` | 1-4 | **F** + **J** |
| `d` | 1-4-5 | **F** + **J** + **K** |
| `l` | 1-2-3 | **F** + **D** + **S** |

When a chord resolves, the translated character is appended to the focus navigator input buffer (visible when an app uses text entry).

## Guided walkthrough

1. **Start the UI** (commands above). Confirm `evdev listening` appears in the log.
2. **Home screen** — press **↓** a few times. Each step announces the focused app (`Document`, `Calculator`, `Wikipedia`, …) on stderr.
3. **Open the menu** — press **F9**. Use **↑ / ↓** to move, **Enter** to select, **Backspace** to go back.
4. **Type braille** — on the home screen, chord **F** (letter `a`). With the default stub build, translation is disabled; use `host-chord-test` or an a11y build for full text (next section).
5. **Activate an app** — focus `Wikipedia` with **↓**, press **Enter**. The app session takes over chord and D-pad routing until you exit.

## Braille text input on the host

The default `make braillatron-ui` build links **stub** backends. Navigation and menus work; chord back-translation is disabled without liblouis.

To enable braille translation on a dev machine (liblouis only — no BRLTTY/Vosk required):

```bash
# Fedora example
sudo dnf install liblouis-devel liblouis

cd mobile-braille-embosser/daemon-dietpi
make BRAILLATRON_LIBLOUIS=1 clean braillatron-ui braillatron-liblouis-test
./braillatron-liblouis-test
BRAILLATRON_CONFIG=config ./braillatron-ui
```

For the full accessibility stack (TTS via Speech Dispatcher, refreshable braille via BRLTTY, STT via Vosk):

```bash
sudo dnf install liblouis-devel speech-dispatcher-devel brltty-devel vosk-devel

make BRAILLATRON_A11Y=1 clean braillatron-ui
BRAILLATRON_CONFIG=config ./braillatron-ui
```

Speech Dispatcher, BRLTTY, and Vosk can still be absent at runtime — the UI falls back to stderr logging for those backends while liblouis handles dot translation.

**Braille grade** (`ui.conf` → `braille_table`) is a global preset cycling through UEB G1/G2 combined with UEB Math or Nemeth: `ueb_g1_math`, `ueb_g1_nemeth`, `ueb_g2_math`, `ueb_g2_nemeth`. Change it from Settings → Braille grade; input, embosser, and refreshable braille all follow the same preset. On systems where the literary+Nemeth composite table fails to compile, Nemeth presets fall back to the matching UEB literary table (override the Nemeth overlay with `LOUIS_NEMETH_TABLE`, default `en-us-mathtext.ctb`).

On a fully provisioned Pi image, bootstrap installs these libraries automatically; see [Pi SD Image Software Build Guide](specs/Pi%20SD%20Image%20Software%20Build%20Guide.md).

## Configuration layout

When `BRAILLATRON_CONFIG` is unset, the daemon reads from `./config/`. Important files:

| File | Purpose |
| --- | --- |
| `hardware.conf` | Serial device, `allow_missing_arduino`, motion enable |
| `keyboard.conf` | Serial + evdev bench input |
| `evdev_map.conf` | USB key → logical key map (edit for non-QWERTY layouts) |
| `ui.conf` | TTS, braille, STT, haptics toggles |

Production paths on the Pi are under `/etc/braillatron/`. See the [Pi SD Image Software Build Guide](specs/Pi%20SD%20Image%20Software%20Build%20Guide.md) for deployment, **testing on the Pi** (no GUI, journal + TTS), bench keyboard setup, and rebuild/update steps.

## Troubleshooting

| Symptom | Fix |
| --- | --- |
| `keyboard: no input sources available` | Set `evdev_enabled=true` in `keyboard.conf` |
| `evdev: no suitable input device found` | Plug in a keyboard; check `ls /dev/input/event*`; ensure F and J keys exist on the device |
| `evdev: unable to open … Permission denied` | Add your user to the `input` group and re-login, or run from a session with input access |
| `EVIOCGRAB failed` | Set `evdev_grab=false`, or run on a console without a competing grab |
| Chords do nothing | Verify timing (release within ~40 ms of press); run `./braillatron-host-chord-test` |
| Chords fire but no letters | Rebuild with `BRAILLATRON_LIBLOUIS=1` and install `liblouis-devel`; run `./braillatron-liblouis-test` |
| Arduino messages on startup | Expected; `allow_missing_arduino=true` keeps the daemon running |

## Firmware (Arduino Micro)

Build and flash the co-processor firmware with [arduino-cli](https://arduino.github.io/arduino-cli/). Full steps: [firmware-arduino/README.md](firmware-arduino/README.md).

```bash
arduino-cli core update-index
arduino-cli core install arduino:avr
make -C firmware-arduino compile
make -C firmware-arduino upload PORT=/dev/ttyACM1
```

Protocol definitions are shared with the Pi daemons in [shared/](shared/) — edit `shared/protocol.h` before changing firmware or daemon parsers.

## Next steps

- **Pi deployment** — [Pi SD Image Software Build Guide](specs/Pi%20SD%20Image%20Software%20Build%20Guide.md)
- **Architecture** — [Master Software Architecture V9](specs/Master%20Software%20Architecture%20V9.md)
- **Serial protocol** — [shared/protocol.md](shared/protocol.md)
- **Firmware** — [firmware-arduino/README.md](firmware-arduino/README.md) (compile and flash with arduino-cli)
