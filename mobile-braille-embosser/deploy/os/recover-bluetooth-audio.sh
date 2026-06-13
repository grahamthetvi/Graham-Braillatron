#!/usr/bin/env bash
# Undo setup-bluetooth-audio.sh if the Pi hangs or SSH becomes unresponsive.
# Run after reboot: sudo bash deploy/os/recover-bluetooth-audio.sh
set -euo pipefail

if [[ "$(id -u)" -ne 0 ]]; then
  echo "recover-bluetooth-audio.sh must run as root (sudo)." >&2
  exit 1
fi

USER_NAME="$(id -un)"
USER_ID="$(id -u)"

echo "Stopping user PipeWire / WirePlumber (common hang source)..."
systemctl -M "${USER_NAME}@" --user stop wireplumber pipewire-pulse pipewire pipewire.socket 2>/dev/null || true
systemctl -M "${USER_NAME}@" --user disable wireplumber pipewire-pulse pipewire pipewire.socket 2>/dev/null || true
systemctl stop user@"${USER_ID}".service 2>/dev/null || true
loginctl disable-linger "${USER_NAME}" 2>/dev/null || true

echo "Stopping Bluetooth stack..."
systemctl stop bluetooth 2>/dev/null || true
systemctl disable bluetooth 2>/dev/null || true

echo "Reverting Speech Dispatcher to ALSA..."
if [[ -f /etc/speech-dispatcher/speechd.conf ]]; then
  sed -i 's/^#* *AudioOutputMethod.*/AudioOutputMethod "alsa"/' /etc/speech-dispatcher/speechd.conf
fi
rm -f /etc/systemd/system/speech-dispatcher.service.d/pipewire.conf
rmdir /etc/systemd/system/speech-dispatcher.service.d 2>/dev/null || true

echo "Restoring /etc/asound.conf from newest backup (if any)..."
latest_backup="$(ls -t /etc/asound.conf.bak.* 2>/dev/null | head -1 || true)"
if [[ -n "${latest_backup}" ]]; then
  cp -a "${latest_backup}" /etc/asound.conf
  echo "Restored ${latest_backup}"
elif [[ -f /etc/asound.conf ]] && grep -q pipewire /etc/asound.conf; then
  rm -f /etc/asound.conf
  echo "Removed PipeWire-only /etc/asound.conf"
fi

systemctl daemon-reload
systemctl restart speech-dispatcher 2>/dev/null || true
systemctl restart braillatron-ui 2>/dev/null || true

echo ""
echo "Recovery complete. Braillatron UI should run (logs only, no speaker until I2S is wired)."
echo "Reboot recommended: reboot"
