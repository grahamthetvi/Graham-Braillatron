#!/usr/bin/env bash
# Default Braillatron audio: 3.5 mm aux jack via ALSA + Speech Dispatcher.
# Cleans up PipeWire user-session experiments and restores a stable headless stack.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [[ "$(id -u)" -ne 0 ]]; then
  echo "setup-aux-audio.sh must run as root (sudo)." >&2
  exit 1
fi

USER_NAME="$(id -un)"
USER_ID="$(id -u)"

echo "Stopping optional PipeWire user session (if present)..."
systemctl -M "${USER_NAME}@" --user stop wireplumber pipewire-pulse pipewire pipewire.socket 2>/dev/null || true
systemctl -M "${USER_NAME}@" --user disable wireplumber pipewire-pulse pipewire pipewire.socket 2>/dev/null || true
systemctl stop "user@${USER_ID}.service" 2>/dev/null || true
loginctl disable-linger "${USER_NAME}" 2>/dev/null || true

rm -f /etc/systemd/system/speech-dispatcher.service.d/pipewire.conf
rmdir /etc/systemd/system/speech-dispatcher.service.d 2>/dev/null || true

if [[ -f /etc/default/speech-dispatcher ]]; then
  sed -i 's/^RUN=no/RUN=yes/' /etc/default/speech-dispatcher || true
fi

install -m 755 "${ROOT}/os/braillatron-audio-select.sh" /usr/local/bin/braillatron-audio-select
install -d /usr/share/braillatron/audio
install -m 644 "${ROOT}/os/audio/asound.aux.conf" /usr/share/braillatron/audio/
install -m 644 "${ROOT}/os/audio/asound.bluetooth.conf" /usr/share/braillatron/audio/
install -m 644 "${ROOT}/os/audio/asound.i2s.conf" /usr/share/braillatron/audio/

echo "Selecting aux jack as default output..."
bash "${ROOT}/os/braillatron-audio-select.sh" aux

systemctl daemon-reload
systemctl enable --now speech-dispatcher 2>/dev/null || true
systemctl restart braillatron-ui 2>/dev/null || true

echo ""
echo "Aux audio configured."
echo "  Test: speaker-test -t sine -f 440 -c 1 -l 1"
echo "  Test: spd-say 'Braillatron aux test'"
echo ""
echo "Bluetooth (optional): sudo bash ${ROOT}/os/setup-bluetooth-audio.sh"
echo "Switch outputs anytime: sudo braillatron-audio-select aux|bluetooth|i2s"
