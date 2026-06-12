# Pi SD Image Software Build Guide

**Target:** Orange Pi 3B (RK3566, aarch64)  
**OS:** DietPi (Debian 13 Trixie, vendor kernel 6.1.115)  
**Goal:** Flash a micro SD card, run one bootstrap pass, reboot, and have Braillatron services start automatically — even with no Arduino co-processor or Pi-side I2C/GPIO peripherals attached.

This guide covers **software only**. For wiring, power, and peripheral pinouts, see [Skeleton Prototype V5.1 Build Guide](Skeleton%20Prototype%20V5.1%20Build%20Guide.md).

For keyboard-only development on a PC (no Pi), see [README.md](../README.md).


## What gets installed

After bootstrap, the SD card provides:

| Component | Path / unit |
| --- | --- |
| UI + ScreenReader hub | `/usr/local/bin/braillatron-ui` → `braillatron-ui.service` |
| Telemetry sentinel | `/usr/local/bin/braillatron-sentinel` → `braillatron-sentinel.service` |
| Connectivity sidecar | `/usr/local/bin/braillatron-connectd` → `braillatron-connectd.service` |
| Document sync (rsync timer) | `/usr/local/bin/braillatron-sync` → `braillatron-sync.timer` |
| Config | `/etc/braillatron/*.conf` |
| Persistent documents | `/data/braillatron/documents/` |
| Settings overrides (RO root) | `/data/braillatron/settings/` |
| Network credentials (YouTube, Signal, etc.) | `/data/braillatron/credentials/` |
| Vosk STT model | `/data/braillatron/vosk-models/vosk-model-small-en-us-0.15/` |
| RAM document layers | `/var/lib/braillatron/ram/` |
| Coordinate session file | `/var/lib/braillatron/ram/coords.json` |
| Live telemetry (sentinel → UI) | `/run/braillatron/telemetry.json` |
| Homing status (sentinel) | `/run/braillatron/homing.status` |
| Connect IPC (connectd ↔ UI) | `/run/braillatron/connect.sock`, `/run/braillatron/connect.events` |

All four daemons/timers are pulled in by `braillatron.target`, which is enabled for multi-user boot:

- `braillatron-ui` — keyboard routing, focus navigation, Output Hub (TTS/braille/STT)
- `braillatron-sentinel` — telemetry, homing, limit sensors (when wired)
- `braillatron-connectd` — YouTube, Messages, and other network sidecar work
- `braillatron-sync.timer` — periodic document rsync

The sentinel performs boot homing when `motion_enabled=true` in `hardware.conf` and writes telemetry for Quick Status / battery warnings.


## Before you start

### Hardware

- Orange Pi 3B (4 GB)
- Micro SD card (32 GB or larger recommended; bootstrap adds a ~768 MB `/data` partition)
- USB power supply (5 V, adequate for Pi + peripherals)
- Ethernet cable **or** Wi‑Fi credentials (needed for first-time package and model downloads)

### Software on your PC

- [DietPi image](https://dietpi.com/) for Orange Pi 3B (Trixie / kernel 6.1.115 if available)
- Flash tool (e.g. `dd`, Raspberry Pi Imager, or Balena Etcher)
- SSH client

### Repository on the Pi

You need the `mobile-braille-embosser` tree on the Pi before running bootstrap. Pick one:

**Option A — Git clone (Pi has network + git):**

```bash
git clone <your-repo-url> braillatron
cd braillatron/mobile-braille-embosser
```

**Option B — Copy from dev machine (no git on Pi):**

```bash
# On your PC (from repo root):
rsync -av --exclude '.git' \
  mobile-braille-embosser/ \
  pi@<pi-ip>:~/braillatron/mobile-braille-embosser/
```


## Step 1: Flash DietPi

1. Download the Orange Pi 3B DietPi image.
2. Flash it to the micro SD card.
3. If your DietPi build supports it, pre-configure hostname, SSH, and network in `dietpi.txt` on the boot partition before first insert.
4. **Reserve tail space for `/data`.** Bootstrap creates a ~768 MB ext4 partition at the end of the SD card. DietPi normally auto-expands the root partition to fill the card on first boot, which leaves no room for `/data` and causes bootstrap to fail. Before the Pi's first power-on, either:
   - Disable root-partition auto-expand in DietPi config (preferred), **or**
   - Leave at least **768 MB unallocated** at the end of the disk when you flash or resize the image.
5. Insert the SD card and power on the Pi.


## Step 2: First boot and SSH

1. Log in (default DietPi credentials depend on image version — check DietPi docs).
2. Confirm network: `ping -c1 deb.debian.org`
3. Confirm architecture: `uname -m` → must show `aarch64`.
4. Confirm free tail space (must be ≥ 768 MB before bootstrap):

```bash
DISK="$(findmnt -n -o SOURCE / | sed 's/p[0-9]*$//')"
sudo parted -ms "${DISK}" unit MB print free
```

Look for a `free` region at the end of the disk. If root already fills the card, shrink the root partition or re-flash with auto-expand disabled before continuing.


## Step 3: One-shot bootstrap

From the `mobile-braille-embosser` directory on the Pi:

```bash
cd ~/braillatron/mobile-braille-embosser   # or your clone path
sudo bash deploy/bootstrap-dietpi.sh
```

This script runs, in order:

1. **`apt install`** — packages from `deploy/packages.txt` (build tools, Speech Dispatcher, BRLTTY, PipeWire, gpiod, parted, rsync, mpv, yt-dlp, etc.)
2. **I2S overlay** — adds `rk3566-i2s1-overlay` to the `overlays=` line in `/boot/dietpiEnv.txt` (MAX98357A audio; see Skeleton Build Guide)
3. **`/data` partition** — `deploy/os/setup-data-partition.sh` creates an ext4 partition labeled `braillatron-data` in unallocated tail space (requires ≥ 768 MB free at the disk end; does not shrink root)
4. **libvosk** — `deploy/install-vosk-lib.sh` installs the prebuilt aarch64 Vosk library (not in Debian apt)
5. **Build + install** — `deploy/install.sh` compiles with `BRAILLATRON_A11Y=1`, installs binaries, configs, and systemd units
6. **signal-cli (optional)** — `deploy/install-signal-cli.sh` for Messages app; skipped gracefully if it fails
7. **Vosk model** — downloads `vosk-model-small-en-us-0.15` (~40 MB) to `/data/braillatron/vosk-models/`
8. **Accessibility stack** — enables `speech-dispatcher`, `brltty`, `pipewire`, `wireplumber`
9. **ALSA default** — appends I2S routing snippet to `/etc/asound.conf`

Bootstrap takes several minutes on first run (apt + compile + model download).


## Step 4: Reboot

```bash
sudo reboot
```

After reboot, `braillatron.target` should start automatically. No login or manual command is required for normal operation.


## Step 5: Verify

```bash
# Services
systemctl status braillatron.target
systemctl status braillatron-ui braillatron-sentinel braillatron-connectd braillatron-sync.timer

# UI log (should announce "Braillatron ready" and list missing devices if hardware absent)
journalctl -u braillatron-ui -b --no-pager | tail -30

# Connect sidecar
journalctl -u braillatron-connectd -b --no-pager | tail -15

# Speech (if I2S amp wired)
spd-say "Braillatron test"

# I2S card (after overlay + reboot)
aplay -l

# Data partition
df -h /data
ls /data/braillatron/
```

### Expected behavior without hardware

With default `allow_missing_arduino=true` in `hardware.conf`, the UI daemon **stays running** when:

- `/dev/ttyACM0` (Arduino co-processor) is absent — chord keys and freefall interlock live on the Arduino; without it there is no keyboard input until evdev bench mode is enabled
- I2C devices (LTC2944, DRV2605L) are absent
- GPIO limit sensors are unconfigured

Status is logged to the journal and announced through the Output Hub (TTS/braille when those backends are available).


## Testing on the Pi

Braillatron has **no graphical UI**. A connected monitor stays blank. After `systemctl restart braillatron-ui`, the command itself prints nothing — that is normal.

Feedback channels:

| Channel | How to use |
| --- | --- |
| **Journal logs** | `journalctl -u braillatron-ui -f` |
| **Speech** | TTS on startup and focus changes (requires I2S amp + Speech Dispatcher) |
| **Braille display** | BRLTTY when a display is connected |

### SSH vs physical keyboard

Bench input reads the **USB keyboard plugged into the Pi** via Linux evdev (`/dev/input/event*`), not keys typed into your SSH session. You can SSH in to watch logs, but navigation requires a keyboard on the Pi itself.

### Quick diagnostic

```bash
systemctl is-active braillatron-ui
journalctl -u braillatron-ui -b --no-pager -n 20
grep evdev /etc/braillatron/keyboard.conf
ls /dev/input/event*
```

Healthy startup includes `[ui] Braillatron ready`, `[ui] Document`, and `braillatron-ui profile=skeleton_v4`.

### Foreground run (debugging)

```bash
sudo systemctl stop braillatron-ui
sudo /usr/local/bin/braillatron-ui /etc/braillatron/hardware.conf
```

Logs stream directly to the terminal. Use the Pi's USB keyboard for input. **Ctrl+C** to quit, then `sudo systemctl start braillatron-ui`.


### Bench testing with a USB keyboard (no wired buttons)

To exercise focus navigation, menu overlay, and braille chord input without the Arduino co-processor or built-in key matrix:

1. Plug a standard USB keyboard into the **Pi**.
2. Edit `/etc/braillatron/keyboard.conf` and set `evdev_enabled=true` (other defaults are fine: `evdev_device=auto`, `evdev_grab=true`).
3. Restart the UI daemon: `sudo systemctl restart braillatron-ui`
4. Watch the log: `journalctl -u braillatron-ui -f`
5. Press **↓** on the Pi keyboard — each focus change should appear as `[ui] Calculator`, etc.

You should see `keyboard: evdev listening on /dev/input/eventN (bench mode)`.

A dev preset lives in the repo at `daemon-dietpi/config/keyboard-bench.conf` (sets `evdev_grab=false` for desktop use; on the Pi, prefer `evdev_grab=true`).

Default key map (`/etc/braillatron/evdev_map.conf`):

| USB key | Action |
| --- | --- |
| Up / Down arrows | Move focus (or menu overlay when open) |
| Enter | Activate focused item / menu entry |
| Backspace | Delete text / back out of menu |
| F7 | Pause/resume speech (press/release) |
| F8 (hold) | Push-to-talk dictation |
| F9 | Toggle global menu overlay |
| F / D / S | Braille dots 1 / 2 / 3 (chord within 40 ms, e.g. F+D) |
| J / K / L | Braille dots 4 / 5 / 6 |

When the real button matrix is wired, set `evdev_enabled=false` or leave both enabled for concurrent bench + hardware testing.

Host-side unit tests (no USB keyboard required):

```bash
cd mobile-braille-embosser/daemon-dietpi
make host-chord-test && ./braillatron-host-chord-test
```


## Updating after code changes

When you have new source on the Pi (git pull or rsync from your dev machine):

```bash
cd ~/braillatron/mobile-braille-embosser/daemon-dietpi
sudo make BRAILLATRON_A11Y=1 clean all
sudo make install          # runs deploy/install.sh
sudo systemctl restart braillatron.target
```

Verify:

```bash
systemctl status braillatron-ui braillatron-connectd
journalctl -u braillatron-ui -b --no-pager | tail -20
```

### Config backup warning

`make install` runs `deploy/install.sh`, which **overwrites** `/etc/braillatron/*.conf` with repo defaults. Back up local edits first:

```bash
sudo cp -a /etc/braillatron /etc/braillatron.bak.$(date +%F)
```

Common settings to restore after install: `evdev_enabled=true` in `keyboard.conf`, custom `arduino_device=` in `hardware.conf`, GPIO paths in `telemetry.conf`.

On a read-only root, remount before building:

```bash
sudo braillatron-remount-rw
# build + install ...
sync
sudo braillatron-remount-ro
```

You do **not** need to re-run full bootstrap for routine code changes — only when `deploy/packages.txt`, vosk install scripts, or OS bootstrap steps change.


## Step 6 (optional): Read-only root

For field deployment and SD wear protection, enable the RO overlay **after** you have verified bootstrap on a read-write root:

```bash
sudo bash deploy/os/setup-overlay-ro.sh
sync
sudo mount -o remount,ro /
sudo reboot
```

Maintenance helpers installed by the overlay script:

- `sudo braillatron-remount-rw` — remount `/` read-write for edits
- `sudo braillatron-remount-ro` — lock root read-only again

`/data` and `/var/lib/braillatron` remain writable for documents and RAM layers.


## Manual build (without full bootstrap)

Same as **Updating after code changes** above. On a fresh Pi that already has bootstrap packages installed, this is the only step needed after pulling new code.

Host-side dev builds (no Pi hardware libs) use stub backends — see [README.md](../README.md):

```bash
make all              # no BRAILLATRON_A11Y
make ui-test && ./braillatron-ui-test
make host-chord-test && ./braillatron-host-chord-test
```


## Configuration reference

| File | Purpose |
| --- | --- |
| `/etc/braillatron/braillatron.conf` | Master config directory pointer |
| `/etc/braillatron/hardware.conf` | Arduino serial path, `allow_missing_arduino`, motion enable, board profile |
| `/etc/braillatron/ui.conf` | TTS, braille, STT, haptics; Vosk model path |
| `/etc/braillatron/telemetry.conf` | I2C, GPIO limit sensors, persistence paths |
| `/etc/braillatron/keyboard.conf` | Pi-side keyboard service; `evdev_enabled` for USB bench input |
| `/etc/braillatron/matrix_map.conf` | Maps Arduino key-state bits to logical key names |
| `/etc/braillatron/evdev_map.conf` | Maps Linux `KEY_*` names to logical Braillatron keys for USB bench input |
| `/etc/braillatron/kinematics.conf` | Motion (disabled on skeleton: `motion_enabled=false`) |
| `/etc/braillatron/connect.conf` | Connect sidecar sockets and credential paths |
| `/etc/braillatron/youtube.conf` | YouTube app settings |
| `/etc/braillatron/messages.conf` | Signal/Messages app settings |
| `/etc/braillatron/library.conf` | Document library paths |
| `/etc/braillatron/localsend.conf` | LocalSend file transfer settings |

Edit configs on a RW root, or remount RW when using RO overlay. Changes under `/etc/braillatron` persist on RW root; on RO root, copy overrides to `/data/braillatron/settings/` and adjust unit `Environment=` if you relocate config (production pattern).


## Troubleshooting

| Symptom | Check |
| --- | --- |
| `systemctl restart` shows nothing | Normal — use `journalctl -u braillatron-ui -f` or `systemctl status braillatron-ui` |
| Monitor is blank after boot | Expected — no GUI; use journal + TTS |
| Keyboard does nothing over SSH | SSH laptop keys don't reach evdev; plug USB keyboard into Pi |
| `braillatron-ui` exits immediately | `journalctl -u braillatron-ui -b`; confirm `/etc/braillatron/hardware.conf` exists |
| `keyboard: no input sources available` | Set `evdev_enabled=true` or connect Arduino |
| Build fails on `spd_set_rate` | Update source — Trixie uses `spd_set_voice_rate` (fixed in current tree) |
| Build fails on `-lvosk` | Re-run `sudo bash deploy/install-vosk-lib.sh`; confirm `aarch64` |
| No speech | `systemctl status speech-dispatcher`; `spd-say test`; I2S overlay in `/boot/dietpiEnv.txt` |
| No audio on speaker | `aplay -l`; `/etc/asound.conf`; wiring per Skeleton Guide (I2S1 pins 35/38/40) |
| Bootstrap fails: "Not enough unallocated space" | DietPi filled the card; disable auto-expand and re-flash, or shrink root to leave ≥ 768 MB at disk end |
| `/data` missing | Re-run `sudo bash deploy/os/setup-data-partition.sh` (review disk layout first) |
| Arduino not detected | `ls -l /dev/ttyACM*`; USB cable; `arduino_device=` in `hardware.conf` |
| STT unavailable | Model dir: `ls /data/braillatron/vosk-models/`; path in `ui.conf` |
| USB keyboard ignored | `evdev_enabled=true`; `ls /dev/input/event*`; `SupplementaryGroups=input` in unit; restart `braillatron-ui` |
| YouTube/Messages unavailable | `systemctl status braillatron-connectd`; `journalctl -u braillatron-connectd -b` |
| Config changes lost after rebuild | `make install` overwrites `/etc/braillatron/` — back up before install |


## Boot flow (summary)

```mermaid
flowchart TD
    PowerOn[SD inserted, power on] --> DietPi[DietPi boots]
    DietPi --> MultiUser[multi-user.target]
    MultiUser --> Target[braillatron.target]
    Target --> UI[braillatron-ui]
    Target --> Sentinel[braillatron-sentinel]
    Target --> Connectd[braillatron-connectd]
    Target --> Timer[braillatron-sync.timer]
    UI --> Hub[Output Hub announces ready / missing devices]
    Connectd --> Sockets[/run/braillatron/connect.sock]
```


## Related docs

- [README.md](../README.md) — dev PC quick start with USB keyboard only
- [Master Software Architecture V9](Master%20Software%20Architecture%20V9.md) — product architecture, OS storage, co-processor protocol
- [Master Architecture V4.9](Master%20Architecture%20V4.9.md) — power topology, TMC2209 daisy chain, PCB lifecycle
- [Skeleton Prototype V5.1 Build Guide](Skeleton%20Prototype%20V5.1%20Build%20Guide.md) — I2S, GPIO, UART, and Klipper wiring
- `deploy/` — `bootstrap-dietpi.sh`, `install.sh`, systemd units, OS scripts
