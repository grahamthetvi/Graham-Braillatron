#!/usr/bin/env bash
# Route Braillatron TTS (Speech Dispatcher) and media (mpv) through a Bluetooth
# speaker via PipeWire/Pulse. Use when no MAX98357A I2S amp is wired.
set -euo pipefail

if [[ "$(id -u)" -ne 0 ]]; then
  echo "setup-bluetooth-audio.sh must run as root (sudo)." >&2
  exit 1
fi

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

# Headless Pi: system services need a logged-in PipeWire session (root linger).
loginctl enable-linger root 2>/dev/null || true
export XDG_RUNTIME_DIR=/run/user/0
install -d -m 700 "${XDG_RUNTIME_DIR}"

systemctl enable --now NetworkManager 2>/dev/null || true
systemctl --user daemon-reload
systemctl --user enable --now pipewire.socket pipewire pipewire-pulse wireplumber

install -d /etc/systemd/system/speech-dispatcher.service.d
cat > /etc/systemd/system/speech-dispatcher.service.d/pipewire.conf <<'EOF'
[Service]
Environment=XDG_RUNTIME_DIR=/run/user/0
Environment=PULSE_SERVER=unix:/run/user/0/pulse/native
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
echo "   wpctl status"
echo "   wpctl set-default <sink-id>    # pick the Bluetooth device"
echo ""
echo "3. Test speech:"
echo "   spd-say 'Braillatron Bluetooth test'"
echo ""
echo "4. Restart UI:"
echo "   systemctl restart braillatron-ui"
echo ""
echo "Optional: enable USB keyboard bench input in /etc/braillatron/keyboard.conf"
echo "  evdev_enabled=true"
