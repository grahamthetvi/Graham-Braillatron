# Mobile Braille Embosser (Braillatron)

Repository overview: [../README.md](../README.md)

Software for the Braillatron portable braille embosser: Pi-side UI daemon, motion/telemetry services, and Arduino co-processor firmware.

This guide gets you from a fresh clone to a working UI on a Linux dev machine using **only a standard USB keyboard** — no Orange Pi, Arduino, or mechanical key matrix required.

## What you can try without hardware

| Feature | Works on a dev PC? |
| --- | --- |
| Focus navigation (home screen apps) | Yes |
| Visual display (UI chrome, ncurses bench) | Yes (`make display` or `BRAILLATRON_DISPLAY=1`) |
| Global menu overlay | Yes |
| Braille chord commit (evdev bench) | Yes (`make host-chord-test`) — commits on release of all dot keys |
| Braille dots → letters | Needs liblouis (see below) |
| Offline apps (Timer, Dictionary, Spelling, Contacts) | Yes — core logic covered by `make check` self-tests |
| connectd network apps (YouTube, Music, Weather, …) | Partial — UI + IPC self-tests; playback/API needs connectd + network on Pi |
| Motion / embossing | No (`motion_enabled=false` in skeleton config) |
| TTS / braille display / STT | Stub backends log to stderr unless you build with `BRAILLATRON_A11Y=1` |

All UI feedback is printed to the terminal as `[ui]`, `[tts]`, `[braille]`, and `[display]` lines. You do not need speakers or a refreshable display to develop. With `make display`, navigation chrome also renders in the terminal via ncurses when stdout is a TTY.

## Prerequisites

- Linux with `g++` (C++17), `make`, and kernel headers (`linux/input-event-codes.h` — usually in `kernel-headers` or `linux-libc-dev`)
- A USB keyboard with a standard QWERTY layout (the bench map uses the **F / D / S** and **J / K / L** home rows for braille dots)
- Read access to `/dev/input/event*` (typically membership in the `input` group, or run from a graphical session on Fedora/Ubuntu)

Optional, for braille-to-text translation on the host:

- `liblouis-devel` (Fedora) or `liblouis-dev` (Debian) — then rebuild with accessibility backends (see **Braille text input** below)
- `libsqlite3-dev` / `sqlite3` (Fedora/Debian) — required for `make dictionary-test` and Dictionary app on Pi

## Build

From the repository root or `daemon-dietpi/`:

```bash
make braillatron-ui          # from daemon-dietpi/
# or
make ui                      # from repo root
```

This produces `braillatron-ui` with stub accessibility backends. For terminal visual chrome during bench development:

```bash
make display   # BRAILLATRON_DISPLAY=1 — ncurses UI chrome when stdout is a TTY
```

Other useful targets:

```bash
make check                                              # build + run 22 host self-tests
make list-tests                                         # index of all self-test binaries
make host-chord-test && ./braillatron-host-chord-test   # evdev chord commit logic, no keyboard
make motion-test && ./braillatron-motion-test           # kinematics math only
make connect-test && ./braillatron-connect-test         # async IPC + connect client
make timer-test && ./braillatron-timer-test             # TimerService
make dictionary-test && ./braillatron-dictionary-test   # SQLite dictionary store (needs libsqlite3-dev)
make spelling-test && ./braillatron-spelling-test       # spelling list store
```

Individual targets also exist for contacts, music, weather, podcasts, radio, library, and gmail backends — all are included in `make check`.

### Self-tests

Run `make check` to build and execute all host self-tests (from repo root or `daemon-dietpi/`). Run `make list-tests` for a full index of binaries, coverage areas, and optional dependencies (`libsqlite3`, `liblouis`).

Optional liblouis self-test (requires `liblouis-dev` / `liblouis-devel`):

```bash
make check-liblouis
```

## Display architecture

Braillatron has two display layers with different audiences and hardware paths.

### Wired local display — `src/ui/display/` (inside `braillatron-ui`)

Renders UI chrome on the physical screen attached to the Pi (fbdev/HDMI, SPI ST7789) or ncurses on a dev bench. Primary audience: sighted users or sighted helpers working alongside the braille user.

Config: [`daemon-dietpi/config/display.conf`](daemon-dietpi/config/display.conf) (`backend`, `fbdev`, `hdmi_enabled`, etc.).

### Wireless remote display — `src/display/` + `braillatron-displayd`

`displayd` serves the browser UI at `:8080` with pairing auth; `braillatron-ui` publishes frames over a Unix socket via `remote_frame_publisher`. Primary audience: development control panel (keyboard/mouse in browser) and remote viewing over LAN.

Config: `/etc/braillatron/remote-display.conf` or `/data/braillatron/settings/remote-display.conf`.

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

### Auto-start on local login (SSH stays a normal shell)

For bench development on a Linux PC, you can launch the ncurses UI automatically when you log in on a **local virtual console** (`tty1`, etc.), while **SSH sessions** keep a regular shell for coding:

```bash
./deploy/os/install-bench-login.sh
```

Log out and back in on the local console (or switch to a text tty with **Ctrl+Alt+F3**). The hook runs `make display` if needed, then starts `braillatron-ui`. **Ctrl+C** returns you to your login shell.

- **SSH** — unaffected; use it for build/edit work as usual
- **Skip once** — `BRAILLATRON_BENCH_AUTO=0 login`
- **Remove hook** — `./deploy/os/install-bench-login.sh --uninstall`

Run the UI manually anytime without the hook (auto-detects liblouis when installed):

```bash
./deploy/os/braillatron-bench-console.sh
```

The bench console script builds with `BRAILLATRON_LIBLOUIS=1` when `pkg-config liblouis` and UEB tables are present; otherwise it falls back to the stub build with a stderr warning.

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
| **`** (grave) | Toggle global menu overlay |
| **Tab** | Pause / resume speech (press and release) |
| **Right Super** (hold) | Push-to-talk dictation gate |
| **F** | Braille dot 1 |
| **D** | Braille dot 2 |
| **S** | Braille dot 3 |
| **J** | Braille dot 4 |
| **K** | Braille dot 5 |
| **L** | Braille dot 6 |

On the **USB keyboard bench path**, braille letters are **chords**: press the dot keys (you can roll them slowly), then release **all** dot keys to commit. The physical Arduino keyboard still uses a **40 ms** integration window (unchanged). Examples (Grade 1 / Perkins):

| Letter | Dots | Keys |
| --- | --- | --- |
| `a` | 1 | **F** |
| `b` | 1-2 | **F** + **D** |
| `c` | 1-4 | **F** + **J** |
| `d` | 1-4-5 | **F** + **J** + **K** |
| `l` | 1-2-3 | **F** + **D** + **S** |

When a chord resolves, the translated character is appended to the home-screen composer line (shown as `> …` above the app list) and to app text fields when an app is active. Unrecognized chords show a toast and boundary haptic.

## Guided walkthrough

1. **Start the UI** (commands above). Confirm `evdev listening` appears in the log.
2. **Home screen** — press **↓** a few times. Each step announces the focused app (`Document`, `Dictionary`, `Spelling`, `Calculator`, `Wikipedia`, `YouTube`, …) on stderr.
3. **Open the menu** — press **`** (grave). Use **↑ / ↓** to move, **Enter** to select, **Backspace** to go back. With a standalone app active, the overlay includes Quick Status, Timer, Morse, Paper Nav, and Save & Exit.
4. **Type braille** — on the home screen, press and release **F** (letter `a` with liblouis). Typed text appears in the `> …` composer line. With the default stub build, you get an "Unrecognized chord" toast instead; use `braillatron-bench-console.sh` or an a11y build for full text (next section).
5. **Activate an app** — focus `Wikipedia` with **↓**, press **Enter**. The app session takes over chord and D-pad routing until you exit.
6. **Document dictation (optional)** — with `BRAILLATRON_A11Y=1` and Vosk installed, enable **Dictation in Document** in Settings; hold **Right Super** (Speech) to dictate into the BRF.

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

**Braille input code** and **Braille grade** are separate `ui.conf` settings (open **`** → Settings). **Braille input code** (`braille_input_table`) selects UEB Math vs Nemeth for keyboard chord back-translation. **Braille grade** (`braille_table`) cycles emboss/output forward translation and refreshable braille through UEB G1/G2 combined with UEB Math or Nemeth: `ueb_g1_math`, `ueb_g1_nemeth`, `ueb_g2_math`, `ueb_g2_nemeth`. On systems where the literary+Nemeth composite table fails to compile, Nemeth presets fall back to the matching UEB literary table (override the Nemeth overlay with `LOUIS_NEMETH_TABLE`, default `en-us-mathtext.ctb`).

On a fully provisioned Pi image, bootstrap installs these libraries automatically and enables **appliance mode** (boot straight into Braillatron, SSH for dev). See [Pi SD Image Software Build Guide](specs/Pi%20SD%20Image%20Software%20Build%20Guide.md).

For a similar local-console experience on a **dev PC** (ncurses bench), use [`deploy/os/install-bench-login.sh`](deploy/os/install-bench-login.sh) — SSH sessions stay a normal shell.

**Pi display surfaces (appliance):**

| Surface | When | What you see |
| --- | --- | --- |
| **Remote display** | Settings → Remote display (default bench path) | UI chrome in a browser at `:8080` with pairing auth; USB keyboard on Pi |
| **SPI panel** | `/dev/spidev0.0` + GPIO configured | Same UI chrome on the HAT display |
| **HDMI** | Opt-in: `hdmi_enabled=true` | UI chrome on `/dev/fb0` (legacy bench; use remote display instead) |
| **SSH** | Always | Admin shell — use `ssh -L 8080:127.0.0.1:8080` when LAN access is disabled |

Default bench without SPI: enable **Remote display** in Settings, show pairing code, open `http://<pi-ip>:8080` on a laptop (or `http://localhost:8080` through an SSH tunnel).

Force TTS-only (no visual UI): `BRAILLATRON_HEADLESS=1` at bootstrap or set in `/etc/braillatron/appliance.env`, then `systemctl restart braillatron.target`. Re-enable visual UI: [`deploy/os/setup-dev-console-mode.sh`](deploy/os/setup-dev-console-mode.sh).

## Configuration layout

When `BRAILLATRON_CONFIG` is unset, the daemon reads from `./config/`. Important files:

| File | Purpose |
| --- | --- |
| `hardware.conf` | Serial device, `allow_missing_arduino`, motion enable |
| `keyboard.conf` | Serial + evdev bench input |
| `evdev_map.conf` | USB key → logical key map (edit for non-QWERTY layouts) |
| `ui.conf` | TTS, braille, STT, haptics toggles, visual display toggle, document dictation |
| `display.conf` | Display backends (`auto`/`spi`/`fb`/`ncurses`/`stub`), spidev, fbdev, GPIO, remote publisher, HDMI opt-in |
| `remote-display.conf` | displayd HTTP/WebSocket listener, pairing, LAN access (`/data/braillatron/settings/` on Pi) |

Production paths on the Pi are under `/etc/braillatron/`. App-specific configs (`dictionary.conf`, `spelling.conf`, `music.conf`, `weather.conf`, `gmail.conf`, etc.) are installed by `deploy/install.sh`. See the [Pi SD Image Software Build Guide](specs/Pi%20SD%20Image%20Software%20Build%20Guide.md) for deployment, **testing on the Pi** (no GUI, journal + TTS), bench keyboard setup, and rebuild/update steps.

Network apps require `braillatron-connectd` running (started by `braillatron.target` on the Pi). On-device bring-up: [Connectivity Follow-Up Checklist](specs/Connectivity%20Follow-Up%20Checklist.md).

## Wi‑Fi on the Pi

Production images use **DietPi ifupdown + wpa_supplicant** on `wlan0` — not NetworkManager. Bootstrap configures this via `deploy/os/setup-dietpi-networking.sh`.

| Task | How |
| --- | --- |
| Join Wi‑Fi from the device | Home screen → **Network and Devices** → type SSID and password on a USB QWERTY keyboard → **Enter** |
| Pre-provision before bootstrap | `sudo bash deploy/os/setup-wifi-credentials.sh 'SSID' 'password'` over SSH, or DietPi `dietpi-wifi.txt` on the SD boot partition |
| Check connection | Menu → **Quick Status** (announces SSID), or `wpa_cli -i wlan0 status` over SSH |
| Ethernet-only bench | `BRAILLATRON_WIFI_BOOT=0` when running `setup-dietpi-networking.sh` |

Full stack details, boot-timing notes, and troubleshooting: [Pi SD Image Guide — Wi‑Fi and network connectivity](specs/Pi%20SD%20Image%20Software%20Build%20Guide.md#wi-fi-and-network-connectivity).

Bluetooth speaker pairing is separate (**Pair Bluetooth** app / Settings → **Pair Bluetooth speaker**); see the Pi guide **Audio output** section.

## Troubleshooting

| Symptom | Fix |
| --- | --- |
| `keyboard: no input sources available` | Set `evdev_enabled=true` in `keyboard.conf` |
| `evdev: no suitable input device found` | Plug in a keyboard; check `ls /dev/input/event*`; ensure F and J keys exist on the device |
| `evdev: unable to open … Permission denied` | Add your user to the `input` group and re-login, or run from a session with input access |
| `EVIOCGRAB failed` | Set `evdev_grab=false`, or run on a console without a competing grab |
| Chords do nothing | Release all dot keys to commit; run `./braillatron-host-chord-test` |
| Chords fire but no letters | Rebuild with `BRAILLATRON_LIBLOUIS=1` and install `liblouis-devel`; run `./braillatron-liblouis-test`, or use `./deploy/os/braillatron-bench-console.sh` |
| Arduino and USB keyboard both connected | Both inputs are processed independently; expect duplicate events if you use both at once |
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
- **connectd bring-up** — [Connectivity Follow-Up Checklist](specs/Connectivity%20Follow-Up%20Checklist.md)
- **Architecture** — [Master Software Architecture V9](specs/Master%20Software%20Architecture%20V9.md)
- **Serial protocol** — [shared/protocol.md](shared/protocol.md)
- **Firmware** — [firmware-arduino/README.md](firmware-arduino/README.md) (compile and flash with arduino-cli)
