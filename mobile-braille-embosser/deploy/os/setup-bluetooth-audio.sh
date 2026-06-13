#!/usr/bin/env bash
# Route Braillatron TTS (Speech Dispatcher) and media (mpv) through a Bluetooth
# speaker via PipeWire/Pulse. Use when no MAX98357A I2S amp is wired.
set -euo pipefail

if [[ "$(id -u)" -ne 0 ]]; then
  echo "setup-bluetooth-audio.sh must run as root (sudo)." >&2
  exit 1
fi

USER_ID="$(id -u)"
USER_NAME="$(id -un)"
USER_BUS="/run/user/${USER_ID}/bus"
USER_MANAGER="user@${USER_ID}.service"

run_user_systemctl() {
  # Works over SSH without a login shell (systemd 248+). Avoid plain
  # `systemctl --user`, which needs DBUS_SESSION_BUS_ADDRESS set.
  systemctl -M "${USER_NAME}@" --user "$@"
}

wait_for_user_bus() {
  local i
  for i in $(seq 1 20); do
    if [[ -S "${USER_BUS}" ]]; then
      return 0
    fi
    sleep 1
  done
  return 1
}

start_user_session() {
  echo "Enabling persistent user session for ${USER_NAME} (required for PipeWire)..."
  loginctl enable-linger "${USER_NAME}" 2>/dev/null || true

  if systemctl is-active --quiet "${USER_MANAGER}"; then
    echo "User manager already running."
    return 0
  fi

  echo "Starting ${USER_MANAGER}..."
  systemctl start "${USER_MANAGER}"

  if wait_for_user_bus; then
    echo "User D-Bus ready at ${USER_BUS}"
    return 0
  fi

  echo ""
  echo "User session bus not available yet (${USER_BUS})." >&2
  echo "This is common on first enable-linger. Reboot, then run this script again:" >&2
  echo "  reboot" >&2
  echo "  sudo bash deploy/os/setup-bluetooth-audio.sh" >&2
  exit 1
}

echo "Installing Bluetooth and PipeWire Pulse compatibility..."
apt-get update
apt-get install -y bluez bluez-tools pipewire-pulse pipewire-alsa

if [[ -f /etc/asound.conf ]] && grep -qE 'hw:1,0|card 1' /etc/asound.conf; then
  backup="/etc/asound.conf.bak.$(date +%Y%m%d%H%M%S)"
  cp -a /etc/asound.conf "${backup}"
  cat > /etc/asound.conf <<'EOF'
# Braillatron: default ALSA output via PipeWire (Bluetooth / jack / USB).
pcm.!default {
  type pipewire
}
ctl.!default {
  type pipewire
}
EOF
  echo "Replaced I2S-only ${backup} with PipeWire default in /etc/asound.conf"
fi

if [[ -f /etc/speech-dispatcher/speechd.conf ]]; then
  sed -i 's/^#* *AudioOutputMethod.*/AudioOutputMethod "pulse"/' /etc/speech-dispatcher/speechd.conf
  echo "Speech Dispatcher: AudioOutputMethod pulse"
fi

systemctl enable --now NetworkManager 2>/dev/null || true
start_user_session

echo "Starting PipeWire (user session)..."
run_user_systemctl daemon-reload
run_user_systemctl enable --now pipewire.socket pipewire pipewire-pulse wireplumber

PULSE_SOCKET="/run/user/${USER_ID}/pulse/native"
if [[ ! -S "${PULSE_SOCKET}" ]]; then
  echo "Waiting for PipeWire Pulse socket..."
  wait_for_user_bus  # reuse wait; pulse may appear slightly later
  sleep 2
fi

install -d /etc/systemd/system/speech-dispatcher.service.d
cat > /etc/systemd/system/speech-dispatcher.service.d/pipewire.conf <<EOF
[Service]
Environment=XDG_RUNTIME_DIR=/run/user/${USER_ID}
Environment=PULSE_SERVER=unix:/run/user/${USER_ID}/pulse/native
EOF

systemctl daemon-reload
systemctl restart speech-dispatcher

echo ""
echo "Bluetooth audio stack configured."
echo ""
echo "1. Pair your speaker (put it in pairing mode first):"
echo "   bluetoothctl"
echo "     power on"
echo "     agent on"
echo "     default-agent"
echo "     scan on"
echo "     pair XX:XX:XX:XX:XX:XX"
echo "     trust XX:XX:XX:XX:XX:XX"
echo "     connect XX:XX:XX:XX:XX:XX"
echo "     quit"
echo ""
echo "2. Set Bluetooth as default output:"
echo "   XDG_RUNTIME_DIR=/run/user/${USER_ID} wpctl status"
echo "   XDG_RUNTIME_DIR=/run/user/${USER_ID} wpctl set-default <sink-id>"
echo ""
echo "3. Test speech:"
echo "   spd-say 'Braillatron Bluetooth test'"
echo ""
echo "4. Restart UI:"
echo "   systemctl restart braillatron-ui"
echo ""
echo "Optional: enable USB keyboard bench input in /etc/braillatron/keyboard.conf"
echo "  evdev_enabled=true"
