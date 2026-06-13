#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [[ "$(id -u)" -ne 0 ]]; then
  echo "bootstrap-dietpi.sh must run as root (sudo)." >&2
  exit 1
fi

echo "Installing packages..."
mapfile -t packages < "${ROOT}/deploy/packages.txt"
apt-get update
apt-get install -y "${packages[@]}"

echo "Configuring I2S overlay..."
if ! grep -q 'rk3566-i2s1-overlay' /boot/dietpiEnv.txt 2>/dev/null; then
  if grep -q '^overlays=' /boot/dietpiEnv.txt 2>/dev/null; then
    # Merge into the existing overlays= line; a duplicate key would override
    # or drop the overlays already configured there.
    sed -i 's/^overlays=.*/& rk3566-i2s1-overlay/' /boot/dietpiEnv.txt
  else
    echo 'overlays=rk3566-i2s1-overlay' >> /boot/dietpiEnv.txt
  fi
fi

if [[ "${BRAILLATRON_SPI_PANEL:-0}" == "1" ]]; then
  echo "Configuring SPI overlay for visual display panel..."
  if ! grep -q 'spidev' /boot/dietpiEnv.txt 2>/dev/null; then
    if grep -q '^overlays=' /boot/dietpiEnv.txt 2>/dev/null; then
      sed -i 's/^overlays=.*/& spi-spidev/' /boot/dietpiEnv.txt
    else
      echo 'overlays=spi-spidev' >> /boot/dietpiEnv.txt
    fi
  fi
else
  echo "SPI overlay skipped (default skeleton bench — HDMI ncurses on tty1)."
  echo "  Re-bootstrap with BRAILLATRON_SPI_PANEL=1 when the SPI HAT is fitted."
fi

bash "${ROOT}/deploy/os/setup-data-partition.sh"
bash "${ROOT}/deploy/install-vosk-lib.sh"
bash "${ROOT}/deploy/install.sh"
bash "${ROOT}/deploy/install-signal-cli.sh" || echo "signal-cli install skipped (optional)"

MODEL_DIR="/data/braillatron/vosk-models/vosk-model-small-en-us-0.15"
if [[ ! -d "${MODEL_DIR}" ]]; then
  echo "Downloading Vosk model..."
  tmp="$(mktemp -d)"
  curl -fsSL -o "${tmp}/vosk-model.zip" \
    https://alphacephei.com/vosk/models/vosk-model-small-en-us-0.15.zip
  unzip -q "${tmp}/vosk-model.zip" -d /data/braillatron/vosk-models/
  rm -rf "${tmp}"
fi

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
bash "${ROOT}/deploy/os/setup-networkmanager.sh"
systemctl enable --now NetworkManager || true

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
  echo "Bootstrap complete. Reboot to run the locked production image."
else
  echo "Bootstrap complete. Reboot when ready (developer image: local login + writable root)."
fi
