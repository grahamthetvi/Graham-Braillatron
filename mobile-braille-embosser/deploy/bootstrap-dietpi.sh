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

echo "Configuring SPI overlay for visual display panel..."
if ! grep -q 'spidev' /boot/dietpiEnv.txt 2>/dev/null; then
  if grep -q '^overlays=' /boot/dietpiEnv.txt 2>/dev/null; then
    sed -i 's/^overlays=.*/& spi-spidev/' /boot/dietpiEnv.txt
  else
    echo 'overlays=spi-spidev' >> /boot/dietpiEnv.txt
  fi
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
systemctl enable --now NetworkManager || true
# Wi-Fi autoconnects in the background; do not block multi-user on DHCP.
systemctl disable NetworkManager-wait-online.service 2>/dev/null || true
systemctl mask NetworkManager-wait-online.service 2>/dev/null || true

bash "${ROOT}/deploy/os/setup-aux-audio.sh"

echo "Bootstrap complete. Review overlay setup:"
echo "  sudo bash ${ROOT}/deploy/os/setup-overlay-ro.sh"
echo "Then reboot."
