# Pi SD Image Software Build Guide

**Target:** Orange Pi 3B (RK3566, aarch64)  
**OS:** DietPi (Debian 13 Trixie, vendor kernel 6.1.115)  
**Goal:** Flash a micro SD card, run one bootstrap pass, reboot, and have Braillatron services start automatically — even with no Arduino co-processor or Pi-side I2C/GPIO peripherals attached.

This guide covers **software only**. For wiring, power, and peripheral pinouts, see [Skeleton Prototype V5.1 Build Guide](file:///home/grahamthetvi/Projects/Graham%20Braillatron/mobile-braille-embosser/specs/Skeleton%20Prototype%20V5.1%20Build%20Guide.md).


## What gets installed

After bootstrap, the SD card provides:

| Component | Path / unit |
| - | - |
| UI + ScreenReader hub | `/usr/local/bin/braillatron-ui` → `braillatron-ui.service` |
| Telemetry sentinel | `/usr/local/bin/braillatron-sentinel` → `braillatron-sentinel.service` |
| Document sync (rsync timer) | `/usr/local/bin/braillatron-sync` → `braillatron-sync.timer` |
| Config | `/etc/braillatron/\*.conf` |
| Persistent documents | `/data/braillatron/documents/` |
| Settings overrides (RO root) | `/data/braillatron/settings/` |
| Vosk STT model | `/data/braillatron/vosk-models/vosk-model-small-en-us-0.15/` |
| RAM document layers | `/var/lib/braillatron/ram/` |
| Coordinate session file | `/var/lib/braillatron/ram/coords.json` |
| Live telemetry (sentinel → UI) | `/run/braillatron/telemetry.json` |
| Homing status (sentinel) | `/run/braillatron/homing.status` |


All three services are pulled in by `braillatron.target`, which is enabled for multi-user boot. The sentinel performs boot homing when `motion_enabled=true` in `hardware.conf` and writes telemetry for Quick Status / battery warnings.


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

```
git clone \<your-repo-url\> braillatron  
cd braillatron/mobile-braille-embosser
```

**Option B — Copy from dev machine (no git on Pi):**

```
\# On your PC (from repo root):  
rsync -av --exclude '.git' mobile-braille-embosser/ pi@\<pi-ip\>:~/braillatron/mobile-braille-embosser/
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

```
DISK="$(findmnt -n -o SOURCE / | sed 's/p\[0-9\]\*$//')"  
sudo parted -ms "$\{DISK\}" unit MB print free
```

Look for a `free` region at the end of the disk. If root already fills the card, shrink the root partition or re-flash with auto-expand disabled before continuing.


## Step 3: One-shot bootstrap

From the `mobile-braille-embosser` directory on the Pi:

```
cd ~/braillatron/mobile-braille-embosser   \# or your clone path  
sudo bash deploy/bootstrap-dietpi.sh
```

This script runs, in order:

1. **`apt install`** — packages from `deploy/packages.txt` (build tools, Speech Dispatcher, BRLTTY, PipeWire, gpiod, parted, rsync, etc.)

2. **I2S overlay** — adds `rk3566-i2s1-overlay` to the `overlays=` line in `/boot/dietpiEnv.txt` (MAX98357A audio; see Skeleton Build Guide)

3. **`/data` partition** — `deploy/os/setup-data-partition.sh` creates an ext4 partition labeled `braillatron-data` in unallocated tail space (requires ≥ 768 MB free at the disk end; does not shrink root)

4. **libvosk** — `deploy/install-vosk-lib.sh` installs the prebuilt aarch64 Vosk library (not in Debian apt)

5. **Build + install** — `deploy/install.sh` compiles with `BRAILLATRON\_A11Y=1`, installs binaries, configs, and systemd units

6. **Vosk model** — downloads `vosk-model-small-en-us-0.15` (~40 MB) to `/data/braillatron/vosk-models/`

7. **Accessibility stack** — enables `speech-dispatcher`, `brltty`, `pipewire`, `wireplumber`

8. **ALSA default** — appends I2S routing snippet to `/etc/asound.conf`

Bootstrap takes several minutes on first run (apt + compile + model download).


## Step 4: Reboot

```
sudo reboot
```

After reboot, `braillatron.target` should start automatically. No login or manual command is required for normal operation.


## Step 5: Verify

```
\# Services  
systemctl status braillatron.target  
systemctl status braillatron-ui braillatron-sentinel braillatron-sync.timer  
  
\# UI log (should announce "Braillatron ready" and list missing devices if hardware absent)  
journalctl -u braillatron-ui -b --no-pager | tail -30  
  
\# Speech (if I2S amp wired)  
spd-say "Braillatron test"  
  
\# I2S card (after overlay + reboot)  
aplay -l  
  
\# Data partition  
df -h /data  
ls /data/braillatron/
```

### Expected behavior without hardware

With default `allow\_missing\_arduino=true` in `hardware.conf`, the UI daemon **stays running** when:

- `/dev/ttyACM0` (Arduino co-processor) is absent — chord keys and freefall interlock live on the Arduino; without it there is no keyboard input, but the Pi stack still boots

- I2C devices (LTC2944, DRV2605L) are absent

- GPIO limit sensors are unconfigured

Status is logged to the journal and announced through the Output Hub (TTS/braille when those backends are available).


### Bench testing with a USB keyboard (no wired buttons)

To exercise focus navigation, menu overlay, and braille chord input without the Arduino co-processor or built-in key matrix:

1. Plug in a standard USB keyboard.
2. Edit `/etc/braillatron/keyboard.conf` and set `evdev_enabled=true` (other defaults are fine: `evdev_device=auto`, `evdev_grab=true`).
3. Restart the UI daemon: `sudo systemctl restart braillatron-ui`
4. Watch the log: `journalctl -u braillatron-ui -f`

You should see `keyboard: evdev listening on /dev/input/eventN (bench mode)`.

Default key map (`/etc/braillatron/evdev_map.conf`):

| USB key | Action |
| - | - |
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

```
cd mobile-braille-embosser/daemon-dietpi
make host-chord-test && ./braillatron-host-chord-test
```


## Step 6 (optional): Read-only root

For field deployment and SD wear protection, enable the RO overlay **after** you have verified bootstrap on a read-write root:

```
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

If you only need to rebuild daemons after a code change:

```
cd mobile-braille-embosser/daemon-dietpi  
sudo make BRAILLATRON\_A11Y=1 clean all  
sudo make install          \# runs deploy/install.sh  
sudo systemctl restart braillatron.target
```

Host-side dev builds (no Pi hardware libs) use stub backends:

```
make all              \# no BRAILLATRON\_A11Y  
make ui-test && ./braillatron-ui-test
make host-chord-test && ./braillatron-host-chord-test
```


## Configuration reference

| File | Purpose |
| - | - |
| `/etc/braillatron/braillatron.conf` | Master config directory pointer |
| `/etc/braillatron/hardware.conf` | Arduino serial path, `allow\_missing\_arduino`, motion enable, board profile |
| `/etc/braillatron/ui.conf` | TTS, braille, STT, haptics; Vosk model path |
| `/etc/braillatron/telemetry.conf` | I2C, GPIO limit sensors, persistence paths |
| `/etc/braillatron/keyboard.conf` | Pi-side keyboard service; `evdev_enabled` for USB bench input |
| `/etc/braillatron/matrix\_map.conf` | Maps Arduino key-state bits to logical key names (identity map for skeleton V5 firmware) |
| `/etc/braillatron/evdev\_map.conf` | Maps Linux `KEY\_\*` names to logical Braillatron keys for USB bench input |
| `/etc/braillatron/kinematics.conf` | Motion (disabled on skeleton: `motion\_enabled=false`) |


Edit configs on a RW root, or remount RW when using RO overlay. Changes under `/etc/braillatron` persist on RW root; on RO root, copy overrides to `/data/braillatron/settings/` and adjust unit `Environment=` if you relocate config (production pattern).


## Troubleshooting

| Symptom | Check |
| - | - |
| `braillatron-ui` exits immediately | `journalctl -u braillatron-ui -b`; confirm `/etc/braillatron/hardware.conf` exists |
| Build fails on `-lvosk` | Re-run `sudo bash deploy/install-vosk-lib.sh`; confirm `aarch64` |
| No speech | `systemctl status speech-dispatcher`; `spd-say test`; I2S overlay in `/boot/dietpiEnv.txt` |
| No audio on speaker | `aplay -l`; `/etc/asound.conf`; wiring per Skeleton Guide (I2S1 pins 35/38/40) |
| Bootstrap fails: "Not enough unallocated space" | DietPi filled the card; disable auto-expand and re-flash, or shrink root to leave ≥ 768 MB at disk end, then re-run bootstrap |
| `/data` missing | Re-run `sudo bash deploy/os/setup-data-partition.sh` (review disk layout first) |
| Arduino not detected | `ls -l /dev/ttyACM\*`; USB cable; `arduino\_device=` in `hardware.conf` |
| STT unavailable | Model dir: `ls /data/braillatron/vosk-models/`; path in `ui.conf` |
| USB keyboard ignored | `evdev_enabled=true` in `keyboard.conf`; `ls /dev/input/event*`; user in `input` group (`SupplementaryGroups=input` in unit); restart `braillatron-ui` |



## Boot flow (summary)

```
flowchart TD  
    PowerOn\[SD inserted, power on\] --\> DietPi\[DietPi boots\]  
    DietPi --\> MultiUser\[multi-user.target\]  
    MultiUser --\> Target\[braillatron.target\]  
    Target --\> UI\[braillatron-ui\]  
    Target --\> Sentinel\[braillatron-sentinel\]  
    Target --\> Timer\[braillatron-sync.timer\]  
    UI --\> Hub\[Output Hub announces ready / missing devices\]
```


## Related docs

- [Mobile Braille Embosser Specification V8](file:///home/grahamthetvi/Projects/Graham%20Braillatron/mobile-braille-embosser/specs/Mobile%20Braille%20Embosser%20Specification%20V8.md) — OS storage and ScreenReader architecture

- [Skeleton Prototype V5.1 Build Guide](file:///home/grahamthetvi/Projects/Graham%20Braillatron/mobile-braille-embosser/specs/Skeleton%20Prototype%20V5.1%20Build%20Guide.md) — I2S, GPIO, UART, and Klipper wiring

- `deploy/` — `bootstrap-dietpi.sh`, `install.sh`, systemd units, OS scripts

