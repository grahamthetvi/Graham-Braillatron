#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=apt-retry.sh
source "${ROOT}/deploy/apt-retry.sh"

if [[ "$(id -u)" -ne 0 ]]; then
  echo "bootstrap-dietpi.sh must run as root (sudo)." >&2
  exit 1
fi

echo "Installing packages..."
mapfile -t packages < "${ROOT}/deploy/packages.txt"
apt_retry_update
apt_retry_install "${packages[@]}"

echo "Configuring I2S overlay..."
if ! grep -q 'rk3566-i2s1-overlay' /boot/dietpiEnv.txt 2>/dev/null; then
  if grep -q '^overlays=' /boot/dietpiEnv.txt 2>/dev/null; then
    sed -i 's/^overlays=.*/& rk3566-i2s1-overlay/' /boot/dietpiEnv.txt
  else
    echo 'overlays=rk3566-i2s1-overlay' >> /boot/dietpiEnv.txt
  fi
fi

echo "Configuring I2C1 overlay (LTC2944, DRV2605L)..."
if ! grep -q 'i2c1' /boot/dietpiEnv.txt 2>/dev/null; then
  if grep -q '^overlays=' /boot/dietpiEnv.txt 2>/dev/null; then
    sed -i 's/^overlays=.*/& i2c1/' /boot/dietpiEnv.txt
  else
    echo 'overlays=i2c1' >> /boot/dietpiEnv.txt
  fi
fi

if [[ "${BRAILLATRON_SPI_PANEL:-0}" == "1" ]]; then
  echo "Configuring SPI3 + spidev overlay for ST7789 panel..."
  if ! grep -qE 'spi3|spidev' /boot/dietpiEnv.txt 2>/dev/null; then
    if grep -q '^overlays=' /boot/dietpiEnv.txt 2>/dev/null; then
      sed -i 's/^overlays=.*/& spi3-spidev/' /boot/dietpiEnv.txt
    else
      echo 'overlays=spi3-spidev' >> /boot/dietpiEnv.txt
    fi
  fi
else
  echo "SPI overlay skipped (default skeleton bench — HDMI framebuffer UI via braillatron-ui.service)."
  echo "  Re-bootstrap with BRAILLATRON_SPI_PANEL=1 when the SPI HAT is fitted."
fi

echo ""
echo "Klipper / Moonraker (Monster8 Option A) — install manually on first Pi bring-up:"
echo "  1. Flash Monster8 with Klipper firmware (see klipper/printer.cfg in repo)"
echo "  2. Install Klipper + Moonraker per https://www.klipper3d.org/ and Moonraker docs"
echo "  3. Copy mobile-braille-embosser/klipper/printer.cfg to ~/printer_data/config/"
echo "  4. Set [mcu] serial to ls /dev/serial/by-id/usb-Klipper_*"
echo "  5. Enable braillatron daemon klipper.conf (enabled=true) after Moonraker responds on :7125"
echo ""

bash "${ROOT}/deploy/os/setup-data-partition.sh"
bash "${ROOT}/deploy/install-vosk-lib.sh"
bash "${ROOT}/deploy/install-yt-dlp.sh"
bash "${ROOT}/deploy/install.sh"
bash "${ROOT}/deploy/install-signal-cli.sh" || echo "signal-cli install skipped (optional)"

bash "${ROOT}/deploy/install-vosk-model.sh"

# Configure speech-dispatcher to run system-wide using ALSA instead of pulse
if [[ -f /etc/default/speech-dispatcher ]]; then
  sed -i 's/^RUN=no/RUN=yes/' /etc/default/speech-dispatcher || true
fi
if [[ -f /etc/speech-dispatcher/speechd.conf ]]; then
  # Force AudioOutputMethod to alsa (often defaults to pulse which fails headless)
  sed -i 's/^#* *AudioOutputMethod.*/AudioOutputMethod "alsa"/' /etc/speech-dispatcher/speechd.conf || true
fi

systemctl enable --now speech-dispatcher || true
systemctl enable --now brltty || true
bash "${ROOT}/deploy/os/setup-dietpi-networking.sh"

bash "${ROOT}/deploy/os/setup-aux-audio.sh"

if [[ "${BRAILLATRON_APPLIANCE:-1}" != "0" ]]; then
  echo "Configuring production appliance mode..."
  BRAILLATRON_HEADLESS="${BRAILLATRON_HEADLESS:-0}" \
  BRAILLATRON_SPI_PANEL="${BRAILLATRON_SPI_PANEL:-0}" \
    bash "${ROOT}/deploy/os/setup-appliance-mode.sh"
else
  echo "Appliance lockdown skipped (BRAILLATRON_APPLIANCE=0)."
  echo "Optional manual overlay setup:"
  echo "  sudo bash ${ROOT}/deploy/os/setup-overlay-ro.sh"
fi

if [[ "${BRAILLATRON_APPLIANCE:-1}" != "0" ]]; then
  echo "Final appliance display checks..."
  bash "${ROOT}/deploy/os/braillatron-systemd-wants.sh"
  systemctl disable braillatron-ui-stub.service 2>/dev/null || true
  systemctl enable braillatron-ui.service 2>/dev/null || true
  BRAILLATRON_SKIP_REBUILD=1 bash "${ROOT}/deploy/os/fix-hdmi-appliance.sh"
  bash "${ROOT}/deploy/verify-install.sh"
  bash "${ROOT}/deploy/verify-display-bootstrap.sh"
  echo "Bootstrap complete. Reboot to run the locked production image."
else
  echo "Bootstrap complete. Reboot when ready (developer image: local login + writable root)."
fi
