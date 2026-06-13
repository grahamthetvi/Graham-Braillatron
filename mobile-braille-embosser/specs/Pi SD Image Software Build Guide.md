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
| RAM document layers (ephemeral tmpfs) | `/var/lib/braillatron/ram/` — lost on reboot; synced to `/data` by `braillatron-sync.timer` |
| Coordinate session file (ephemeral tmpfs) | `/var/lib/braillatron/ram/coords.json` |
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
3. **SPI overlay (optional)** — adds `spi-spidev` only when `BRAILLATRON_SPI_PANEL=1` (HAT fitted). Default bootstrap leaves SPI off so `/dev/spidev0.0` is absent and HDMI framebuffer UI works on skeleton bench
4. **`/data` partition** — `deploy/os/setup-data-partition.sh` creates an ext4 partition labeled `braillatron-data` in unallocated tail space (requires ≥ 768 MB free at the disk end; does not shrink root)
5. **libvosk** — `deploy/install-vosk-lib.sh` installs the prebuilt aarch64 Vosk library (not in Debian apt)
6. **Build + install** — `deploy/install.sh` compiles with `BRAILLATRON_A11Y=1`, installs binaries, configs, and systemd units
7. **signal-cli (optional)** — `deploy/install-signal-cli.sh` for Messages app; skipped gracefully if it fails
8. **Vosk model** — downloads `vosk-model-small-en-us-0.15` (~40 MB) to `/data/braillatron/vosk-models/`
9. **Accessibility stack** — enables `speech-dispatcher`, `brltty`, `NetworkManager`
10. **Audio default** — `setup-aux-audio.sh` routes ALSA + TTS to the 3.5 mm aux jack; optional Bluetooth via `setup-bluetooth-audio.sh`
11. **Appliance mode** — `setup-appliance-mode.sh` disables local console login, routes display (SPI + HDMI framebuffer / headless stub), enables read-only root, keeps SSH (skipped when `BRAILLATRON_APPLIANCE=0`)

Bootstrap takes several minutes on first run (apt + compile + model download).


## Step 5: Reboot

```bash
sudo reboot
```

After reboot, `braillatron.target` starts automatically. **No login or manual command is required** — power on, wait for TTS “Braillatron ready”, then use the physical keyboard.

**HDMI monitor (no SPI panel):** boot scroll clears from tty1, then **UI chrome** renders on HDMI via `/dev/fb0` (`braillatron-ui.service`). tty1 stays blank on success (no figlet banner). This is automatic when SPI is not configured. Seeing `Reached target Graphical Interface` in the boot log is normal DietPi noise — the product UI uses the Linux framebuffer, not `graphical.target`.

**SPI panel present:** UI chrome renders on the panel and on HDMI when both are available.

**TTS-only (no HDMI UI):** bootstrap with `BRAILLATRON_HEADLESS=1` or edit `/etc/braillatron/appliance.env`.

Use SSH for development and maintenance (see below).


## Step 6: Verify

```bash
# Services
systemctl status braillatron.target getty@tty1.service
systemctl status braillatron-ui braillatron-ui-stub \
  braillatron-sentinel braillatron-connectd braillatron-sync.timer

# UI service should be active (stub only when headless)
systemctl is-active braillatron-ui braillatron-ui-stub

# Boot-path diagnostics (blank monitor)
sudo braillatron-boot-diagnose.sh

# UI log (display backend line confirms framebuffer/SPI routing)
journalctl -u braillatron-ui -b --no-pager | grep '\[display\] backend='
journalctl -u braillatron-ui -b --no-pager | tail -30
journalctl -u getty@tty1.service -b --no-pager | tail -15

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
| **SPI panel** | `braillatron-ui.service` — UI chrome on the HAT when `/etc/braillatron/appliance-spi` exists (`BRAILLATRON_SPI_PANEL=1` at bootstrap) |
| **HDMI (framebuffer)** | `braillatron-ui.service` — UI chrome on `/dev/fb0` (default skeleton bench) |
| **SPI + HDMI** | Same service — composite backend when both devices are available |
| **Headless override** | `braillatron-ui-stub.service` when `BRAILLATRON_HEADLESS=1` — TTS + keyboard, no visual UI |
| **SSH** | Normal shell for builds, config edits, and debugging |

**TTY glossary:** `tty1` is the HDMI text console. SSH sessions use a pseudo-TTY (`pts/N`) — a separate terminal for admin work, not the monitor.

**Develop over SSH:**

```bash
ssh dietpi@<device-ip>

# View logs (no remount needed)
journalctl -u braillatron-ui -f

# Edit system config or reinstall daemons
sudo braillatron-remount-rw
sudo nano /etc/braillatron/ui.conf          # example
sudo systemctl restart braillatron.target
sudo braillatron-remount-ro

# User documents — always writable
ls /data/braillatron/documents/
```

**Display overrides:**

```bash
# SPI HAT fitted (enables spi-spidev overlay and SPI UI routing)
BRAILLATRON_SPI_PANEL=1 sudo bash deploy/bootstrap-dietpi.sh

# TTS-only skeleton (no visual UI)
BRAILLATRON_HEADLESS=1 sudo bash deploy/bootstrap-dietpi.sh

# Re-enable visual UI after headless
sudo bash deploy/os/setup-dev-console-mode.sh && sudo reboot
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

On the **default skeleton bench** (no SPI HAT), a connected HDMI monitor shows **UI chrome** via `/dev/fb0` (`braillatron-ui.service`). tty1 is cleared on success; error text appears on tty1 only when the UI fails to start. Speech, BRLTTY, and journal logs remain available regardless of display path.

Feedback channels:

| Channel | How to use |
| --- | --- |
| **HDMI framebuffer** | UI chrome on `/dev/fb0` via `braillatron-ui.service` (default skeleton bench) |
| **tty1** | Cleared on success; error text only when UI or display backend fails |
| **Journal logs** | `journalctl -u braillatron-ui -f` |
| **Speech** | TTS on startup and focus changes (aux jack, Bluetooth, or I2S + Speech Dispatcher) |
| **Braille display** | BRLTTY when a display is connected |

### SSH vs physical keyboard

Bench input reads the **USB keyboard plugged into the Pi** via Linux evdev (`/dev/input/event*`), not keys typed into your SSH session. You can SSH in to watch logs, but navigation requires a keyboard on the Pi itself.

### Quick diagnostic

```bash
systemctl is-active braillatron-ui braillatron-ui-stub
journalctl -u braillatron-ui -b --no-pager | grep '\[display\] backend='
journalctl -u braillatron-ui -b --no-pager -n 20
grep evdev /etc/braillatron/keyboard.conf
ls /dev/input/event*
test -f /etc/braillatron/appliance-spi && echo spi-panel || echo hdmi-framebuffer
```

Healthy startup includes `[ui] Braillatron ready`, `[ui] Document`, and `braillatron-ui profile=skeleton_v4`.

### Foreground run (debugging)

```bash
sudo systemctl stop braillatron-ui braillatron-ui-stub
sudo /usr/local/bin/braillatron-ui /etc/braillatron/hardware.conf
```

Logs stream directly to the terminal. Use the Pi's USB keyboard for input. **Ctrl+C** to quit, then `sudo systemctl start braillatron.target`.


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

`/data` remains writable for documents, settings, and credentials. `/var/lib/braillatron` is a tmpfs mount (session RAM state) and is recreated empty on each boot.


## Repair existing appliance images

After pulling boot-hardening updates, re-apply appliance setup once (fixes duplicate fstab entries, read-only root, getty lockdown, and stale SPI overlay):

```bash
sudo braillatron-remount-rw    # skip if root is still rw
cd ~/braillatron/mobile-braille-embosser
git pull
sudo bash deploy/os/setup-appliance-mode.sh
grep -E 'tmpfs|/ ext4' /etc/fstab
sudo reboot
```

After reboot, verify:

```bash
findmnt -o TARGET,OPTIONS / /var/lib/braillatron /tmp /data
dmesg | grep -i 'duplicate entry'    # expect empty
systemctl is-active braillatron-ui braillatron-ui-stub
journalctl -u braillatron-ui -b --no-pager | grep '\[display\] backend='
```


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
| Monitor is blank after boot | `sudo braillatron-boot-diagnose.sh`; `journalctl -u braillatron-ui -b | grep '\[display\] backend='`; check `/dev/fb0`, `display.conf` `hdmi_enabled=true`, and `braillatron-ui` `video` group |
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
| Monitor frozen on last boot line (`graphical.target`) | Boot may be done — SSH in and check `systemctl is-active braillatron-ui` and `journalctl -u braillatron-ui -b | grep backend=` |
| Monitor shows DietPi "hit return to login" but Enter does nothing | Expected when getty is disabled — re-run `setup-appliance-mode.sh`; use SSH or USB keyboard on the Pi |
| No framebuffer UI on HDMI after reboot | `sudo braillatron-boot-diagnose.sh`; check `test -f /etc/braillatron/appliance-headless && echo headless`; `journalctl -u braillatron-ui -b | grep backend=` (expect `fb` or `spi+fb`, not `stub`); `/dev/fb0` present; `grep hdmi_enabled /etc/braillatron/display.conf`; run `sudo bash deploy/os/setup-appliance-mode.sh` and `sudo systemctl restart braillatron.target`. Stale `spi-spidev` in `/boot/dietpiEnv.txt` without a HAT — remove overlay, ensure `appliance-spi` absent, reboot |
| No local login prompt after bootstrap | Expected in appliance mode — use SSH; re-flash or `BRAILLATRON_APPLIANCE=0` bootstrap for dev image |
| SSH unreachable after bootstrap | Check IP and network (`nmcli dev wifi`); confirm `systemctl status ssh`; if locked out entirely, re-flash SD and use `BRAILLATRON_APPLIANCE=0` bootstrap for a dev image with local login |
| `systemd-fstab-generator` duplicate entry warnings in `dmesg` | Duplicate `/tmp` or `/var/log` fstab lines (DietPi + braillatron); run `sudo bash deploy/os/setup-appliance-mode.sh` or `setup-overlay-ro.sh`, then reboot |
| Cannot edit `/etc/braillatron/` over SSH | Run `sudo braillatron-remount-rw` first, then `sudo braillatron-remount-ro` when done |


## Boot flow (summary)

```mermaid
flowchart TD
    PowerOn[SD inserted, power on] --> DietPi[DietPi boots]
    DietPi --> MultiUser[multi-user.target]
    MultiUser --> Target[braillatron.target]
    MultiUser --> Getty[getty@tty1]
    Target --> SpiCheck{appliance-spi?}
    SpiCheck -->|yes| UI[braillatron-ui SPI and/or HDMI]
    SpiCheck -->|no| HeadlessCheck{appliance-headless?}
    HeadlessCheck -->|no| UI
    HeadlessCheck -->|yes| StubUI[braillatron-ui-stub TTS only]
    Getty --> TtyLaunch[braillatron-tty1-launch.sh]
    TtyLaunch --> WaitUI[Wait for UI service]
    WaitUI -->|ok| ClearTTY[Clear tty1 on success]
    WaitUI -->|fail| ErrorTTY[Error text on tty1]
    Target --> Sentinel[braillatron-sentinel]
    Target --> Connectd[braillatron-connectd]
    Target --> Timer[braillatron-sync.timer]
    UI --> Ready[TTS: Braillatron ready]
    StubUI --> Ready
    Ready --> Use[Physical keyboard + Output Hub]
    MultiUser --> SSH[ssh.service for dev access]
```


## Related docs

- [README.md](../README.md) — dev PC quick start with USB keyboard only
- [Master Software Architecture V9](Master%20Software%20Architecture%20V9.md) — product architecture, OS storage, co-processor protocol
- [Master Architecture V4.9](Master%20Architecture%20V4.9.md) — power topology, TMC2209 daisy chain, PCB lifecycle
- [Skeleton Prototype V5.1 Build Guide](Skeleton%20Prototype%20V5.1%20Build%20Guide.md) — I2S, GPIO, UART, and Klipper wiring
- `deploy/` — `prepare-sd-card.py` (PC SD prep), `bootstrap-dietpi.sh`, `install.sh`, systemd units, OS scripts
