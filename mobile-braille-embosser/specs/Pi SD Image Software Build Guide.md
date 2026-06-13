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
- This repository (for `deploy/prepare-sd-card.py` on the PC, or rsync to the Pi in Step 3)


## Step 1: Flash DietPi

1. Download the Orange Pi 3B DietPi image.
2. Flash it to the micro SD card.
3. If your DietPi build supports it, pre-configure hostname, SSH, and network in `dietpi.txt` on the boot partition before first insert.
4. **Reserve tail space for `/data`** (see below) — bootstrap needs ≥ 768 MB unallocated at the end of the SD card.
5. Insert the SD card and power on the Pi.

### PC helper: `deploy/prepare-sd-card.py` (recommended)

DietPi normally **auto-expands root to fill the whole card on first boot**. Bootstrap then has nowhere to create the persistent `/data` partition and fails. The repo includes a Python script that prepares the card **on your PC, after flashing and before the Pi's first power-on**:

`mobile-braille-embosser/deploy/prepare-sd-card.py`

It will:

1. Find your removable micro SD card (never your system disk).
2. Expand root to use the card **minus** a 768 MB tail reserved for `/data`.
3. Write `/dietpi_skip_partition_resize` so DietPi does not re-expand root to 100% on boot.
4. Verify the tail gap is still ≥ 768 MB.

Typical workflow (from a clone of this repo on your PC):

```bash
cd mobile-braille-embosser

# See removable cards; pick the right device
sudo python3 deploy/prepare-sd-card.py --list

# Prepare the flashed card (prompts unless -y)
sudo python3 deploy/prepare-sd-card.py --disk /dev/sdX

# Or let the script auto-select a ~32 GB removable card
sudo python3 deploy/prepare-sd-card.py -y
```

Useful flags (full list: `python3 deploy/prepare-sd-card.py --help`):

| Flag | Purpose |
| --- | --- |
| `--list` | List candidate SD cards and exit |
| `--disk PATH` | Target a specific block device (e.g. `/dev/mmcblk0`) |
| `--dry-run` | Show planned changes without writing |
| `--shrink-if-needed` | Repair a card whose root already expanded to fill the disk |
| `-y` / `--yes` | Skip confirmation prompt |

**Requirements on the PC:** Python 3, `sudo`, and standard partition tools (`parted`, `e2fsprogs`, `util-linux` — usually already present on Linux).

If you skip this script, you can instead manually create `/dietpi_skip_partition_resize` on the flashed root partition and confirm ≥ 768 MB tail space, or leave at least **768 MB unallocated** at the end of the disk when you flash or resize the image. If the Pi already booted once and root filled the card, re-flash or run the script with `--shrink-if-needed` from your PC (see troubleshooting).


## Step 2: First boot and SSH

1. Log in (default DietPi credentials depend on image version — check DietPi docs).
2. Confirm network: `ping -c1 deb.debian.org`
3. Confirm architecture: `uname -m` → must show `aarch64`.
4. Confirm root has room for DietPi-Upgrade and bootstrap, and that tail space remains for `/data`:

```bash
df -h /
DISK="$(findmnt -n -o SOURCE / | sed 's/p[0-9]*$//')"
sudo parted -ms "${DISK}" unit MB print free
```

`/` should show most of the card free (roughly 30+ GB on a 32 GB card). Look for a `free` region at the end of the disk (≥ 768 MB). Example of a good layout on a 32 GB card (800 MB tail — proceed to Step 3):

```
Filesystem      Size  Used Avail Use% Mounted on
/dev/mmcblk1p1   28G  764M   26G   3% /
...
1:30500MB:31300MB:800MB:free;
```

If `/` is only a few GB while tens of GB are unallocated, expand root before upgrading or bootstrapping:

```bash
sudo bash deploy/os/expand-root-reserve-data.sh
```

If root already fills the card with no tail gap, re-flash and run `sudo python3 deploy/prepare-sd-card.py --shrink-if-needed` on your PC (see **PC helper** in Step 1).


## Step 3: Get the repository on the Pi

You need the `mobile-braille-embosser` tree on the Pi before bootstrap. Fresh DietPi images do **not** include `git` — install it first if you use Option A, or use Option B and skip git entirely.

**Option A — Git clone on the Pi:**

```bash
sudo apt update
sudo apt install -y git
git clone https://github.com/grahamthetvi/Graham-Braillatron.git braillatron
cd braillatron/mobile-braille-embosser
```

**Option B — Copy from your dev machine (no git on the Pi):**

```bash
# On your PC (from repo root). Use the SSH user you log in with (root, dietpi, etc.):
rsync -av --exclude '.git' \
  mobile-braille-embosser/ \
  root@<pi-ip>:~/braillatron/mobile-braille-embosser/
```

Then on the Pi:

```bash
cd ~/braillatron/mobile-braille-embosser
```


## Step 4: One-shot bootstrap

From the `mobile-braille-embosser` directory on the Pi:

```bash
cd ~/braillatron/mobile-braille-embosser   # or your clone path
sudo bash deploy/bootstrap-dietpi.sh
```

This script runs, in order:

1. **`apt install`** — packages from `deploy/packages.txt` (build tools, Speech Dispatcher, BRLTTY, BlueZ, gpiod, parted, rsync, mpv, yt-dlp, etc.)
2. **I2S overlay** — adds `rk3566-i2s1-overlay` to the `overlays=` line in `/boot/dietpiEnv.txt` (MAX98357A audio; see Skeleton Build Guide)
3. **`/data` partition** — `deploy/os/setup-data-partition.sh` creates an ext4 partition labeled `braillatron-data` in unallocated tail space (requires ≥ 768 MB free at the disk end; does not shrink root)
4. **libvosk** — `deploy/install-vosk-lib.sh` installs the prebuilt aarch64 Vosk library (not in Debian apt)
5. **Build + install** — `deploy/install.sh` compiles with `BRAILLATRON_A11Y=1`, installs binaries, configs, and systemd units
6. **signal-cli (optional)** — `deploy/install-signal-cli.sh` for Messages app; skipped gracefully if it fails
7. **Vosk model** — downloads `vosk-model-small-en-us-0.15` (~40 MB) to `/data/braillatron/vosk-models/`
8. **Accessibility stack** — enables `speech-dispatcher`, `brltty`, `NetworkManager`
9. **Audio default** — `setup-aux-audio.sh` routes ALSA + TTS to the 3.5 mm aux jack; optional Bluetooth via `setup-bluetooth-audio.sh`
10. **Appliance mode** — `setup-appliance-mode.sh` disables local console login, enables read-only root, keeps SSH for development (skipped when `BRAILLATRON_APPLIANCE=0`)

Bootstrap takes several minutes on first run (apt + compile + model download).


## Step 5: Reboot

```bash
sudo reboot
```

After reboot, `braillatron.target` starts automatically. **No login or manual command is required** — the device is in appliance mode: power on, wait for TTS “Braillatron ready”, then use the physical keyboard.

An attached monitor will stay blank (no login prompt). Use SSH for development and maintenance (see below).


## Step 6: Verify

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


## Appliance mode and SSH development

Production bootstrap locks the device into appliance mode automatically:

| Surface | What you get |
| --- | --- |
| **Local (power on)** | `braillatron-ui` via systemd; TTS “Braillatron ready”; physical keyboard; no login prompt |
| **SSH** | Normal shell for builds, config edits, and debugging |

**Develop over SSH:**

```bash
ssh dietpi@<device-ip>

# View logs (no remount needed)
journalctl -u braillatron-ui -f

# Edit system config or reinstall daemons
sudo braillatron-remount-rw
sudo nano /etc/braillatron/ui.conf          # example
sudo systemctl restart braillatron-ui
sudo braillatron-remount-ro

# User documents — always writable
ls /data/braillatron/documents/
```

**Skip appliance lockdown** during initial factory bring-up (local login + writable root):

```bash
BRAILLATRON_APPLIANCE=0 sudo bash deploy/bootstrap-dietpi.sh
```

See [Master Software Architecture V9 §6.2](Master%20Software%20Architecture%20V9.md#62-appliance-console-vs-dev-ssh) for the full policy.


## Audio output (aux default, Bluetooth optional)

Bootstrap configures the **3.5 mm aux jack** as the default route (ALSA + Speech Dispatcher). This works on a skeleton bench without I2S or Bluetooth.

```bash
# Restore aux default after experiments or a freeze:
sudo bash deploy/os/recover-bluetooth-audio.sh
# same as:
sudo bash deploy/os/setup-aux-audio.sh

speaker-test -t sine -f 440 -c 1 -l 1   # Ctrl+C to stop
spd-say "Braillatron aux test"
sudo systemctl restart braillatron-ui
```

Switch outputs anytime:

```bash
sudo braillatron-audio-select aux          # 3.5 mm jack (default)
sudo braillatron-audio-select bluetooth  # paired BT speaker (BlueALSA)
sudo braillatron-audio-select i2s        # MAX98357A I2S amp (production)
sudo braillatron-audio-select status
```

From the device menu (grave **`** → Settings):

- **Audio output** — switch between aux jack, Bluetooth speaker, and I2S amplifier; reconnect a saved Bluetooth speaker
- **Pair Bluetooth speaker** — enter the speaker MAC address (colons optional); pairs, saves, and switches to Bluetooth output

SSH scripts above remain for advanced recovery and bench setup.

**Optional Bluetooth** (does not change default until you select it):

```bash
sudo bash deploy/os/setup-bluetooth-audio.sh
# pair your speaker, then:
sudo braillatron-audio-select bluetooth
spd-say "Braillatron Bluetooth test"
sudo braillatron-audio-select aux   # back to aux jack
```

Bluetooth uses **BlueALSA** (system service) instead of PipeWire user sessions, which avoids headless SSH freezes.

Production hardware with the **MAX98357A I2S amp** still uses `rk3566-i2s1-overlay` in `/boot/dietpiEnv.txt`; run `sudo braillatron-audio-select i2s` after wiring.


## Testing on the Pi

Braillatron has **no graphical UI**. A connected monitor stays blank. After `systemctl restart braillatron-ui`, the command itself prints nothing — that is normal.

Feedback channels:

| Channel | How to use |
| --- | --- |
| **Journal logs** | `journalctl -u braillatron-ui -f` |
| **Speech** | TTS on startup and focus changes (aux jack, Bluetooth, or I2S + Speech Dispatcher) |
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
| ` (grave) | Toggle global menu overlay |
| Tab | Pause/resume speech (press/release) |
| Right Super (hold) | Push-to-talk dictation |
| F / D / S | Braille dots 1 / 2 / 3 (chord within 40 ms, e.g. F+D) |
| J / K / L | Braille dots 4 / 5 / 6 |

When the real button matrix is wired, set `evdev_enabled=false` or leave both enabled for concurrent bench + hardware testing.

Host-side unit tests (no USB keyboard required):

```bash
cd mobile-braille-embosser/daemon-dietpi
make host-chord-test && ./braillatron-host-chord-test
```


## Updating after code changes

When you have new source on the Pi (`git pull` — requires git from Step 3 Option A — or rsync from your dev machine as in Step 3 Option B):

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


## Step 7 (optional): Read-only root

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
| `/etc/braillatron/ui.conf` | TTS, braille, STT, haptics, visual display toggle; Vosk model path |
| `/etc/braillatron/display.conf` | Visual display backend (`auto`/`spi`/`ncurses`/`stub`), `/dev/spidev0.0`, GPIO placeholders |
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
| No speech | `systemctl status speech-dispatcher`; `spd-say test`; `sudo braillatron-audio-select status`; aux jack + `/etc/asound.conf` |
| No audio on aux | `aplay -l`; `sudo bash deploy/os/setup-aux-audio.sh`; try `Braillatron_AUX_CARD=1 sudo braillatron-audio-select aux` if card 0 is wrong |
| Bluetooth silent | `sudo braillatron-audio-select bluetooth`; `bluetoothctl connect <MAC>`; `systemctl status bluealsa` |
| DietPi-Upgrade fails: "No space left on device" | Root stayed at the small flashed size; run `sudo bash deploy/os/expand-root-reserve-data.sh`, then `sudo apt-get clean && sudo dpkg --configure -a` |
| `git: command not found` on fresh DietPi | Install git: `sudo apt update && sudo apt install -y git`, or use Step 3 Option B (rsync) |
| Bootstrap fails: "Not enough unallocated space" | DietPi filled the card; re-flash and run `sudo python3 deploy/prepare-sd-card.py --shrink-if-needed` on your PC (Step 1) |
| `/data` missing | Re-run `sudo bash deploy/os/setup-data-partition.sh` (review disk layout first) |
| Arduino not detected | `ls -l /dev/ttyACM*`; USB cable; `arduino_device=` in `hardware.conf` |
| STT unavailable | Model dir: `ls /data/braillatron/vosk-models/`; path in `ui.conf` |
| USB keyboard ignored | `evdev_enabled=true`; `ls /dev/input/event*`; `SupplementaryGroups=input` in unit; restart `braillatron-ui` |
| YouTube/Messages unavailable | `systemctl status braillatron-connectd`; `journalctl -u braillatron-connectd -b` |
| Config changes lost after rebuild | `make install` overwrites `/etc/braillatron/` — back up before install |
| Monitor shows DietPi "hit return to login" but Enter does nothing | Expected when appliance mode disabled getty — not a login failure; use USB keyboard on Pi + TTS; verify `braillatron-ui` over SSH |
| No local login prompt after bootstrap | Expected in appliance mode — use SSH; re-flash or `BRAILLATRON_APPLIANCE=0` bootstrap for dev image |
| SSH unreachable after bootstrap | Check IP and network (`nmcli dev wifi`); confirm `systemctl status ssh`; if locked out entirely, re-flash SD and use `BRAILLATRON_APPLIANCE=0` bootstrap for a dev image with local login |
| Cannot edit `/etc/braillatron/` over SSH | Run `sudo braillatron-remount-rw` first, then `sudo braillatron-remount-ro` when done |


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
    UI --> Ready[TTS: Braillatron ready]
    Ready --> Use[Physical keyboard + Output Hub]
    MultiUser --> SSH[ssh.service for dev access]
```


## Related docs

- [README.md](../README.md) — dev PC quick start with USB keyboard only
- [Master Software Architecture V9](Master%20Software%20Architecture%20V9.md) — product architecture, OS storage, co-processor protocol
- [Master Architecture V4.9](Master%20Architecture%20V4.9.md) — power topology, TMC2209 daisy chain, PCB lifecycle
- [Skeleton Prototype V5.1 Build Guide](Skeleton%20Prototype%20V5.1%20Build%20Guide.md) — I2S, GPIO, UART, and Klipper wiring
- `deploy/` — `prepare-sd-card.py` (PC SD prep), `bootstrap-dietpi.sh`, `install.sh`, systemd units, OS scripts
