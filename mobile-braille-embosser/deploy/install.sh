#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DAEMON_DIR="${ROOT}/daemon-dietpi"
PREFIX="${PREFIX:-/usr/local}"
CONFIG_DIR="${CONFIG_DIR:-/etc/braillatron}"
SYSTEMD_DIR="/etc/systemd/system"

if [[ "$(id -u)" -ne 0 ]]; then
  echo "install.sh must run as root (sudo)." >&2
  exit 1
fi

# libvosk is not packaged for aarch64 Debian; install prebuilt headers/libs before A11Y build.
if [[ "$(uname -m)" == "aarch64" ]]; then
  bash "${ROOT}/deploy/install-vosk-lib.sh"
fi

echo "Building Braillatron daemons (accessibility backends enabled)..."
make -C "${DAEMON_DIR}" BRAILLATRON_A11Y=1 BRAILLATRON_DISPLAY=1 clean all

install -d "${PREFIX}/bin"
install -m 755 "${DAEMON_DIR}/braillatron-ui" "${PREFIX}/bin/braillatron-ui"
install -m 755 "${DAEMON_DIR}/braillatron-sentinel" "${PREFIX}/bin/braillatron-sentinel"
install -m 755 "${DAEMON_DIR}/braillatron-connectd" "${PREFIX}/bin/braillatron-connectd"
install -m 755 "${DAEMON_DIR}/braillatron-displayd" "${PREFIX}/bin/braillatron-displayd"
install -m 755 "${ROOT}/deploy/os/sync-documents.sh" "${PREFIX}/bin/braillatron-sync"
install -m 755 "${ROOT}/deploy/os/braillatron-audio-select.sh" "${PREFIX}/bin/braillatron-audio-select"
install -m 755 "${ROOT}/deploy/os/braillatron-bluetooth-autoconnect.sh" "${PREFIX}/bin/braillatron-bluetooth-autoconnect"

install -d /usr/share/braillatron/audio
install -m 644 "${ROOT}/deploy/os/audio/asound.aux.conf" /usr/share/braillatron/audio/
install -m 644 "${ROOT}/deploy/os/audio/asound.bluetooth.conf" /usr/share/braillatron/audio/
install -m 644 "${ROOT}/deploy/os/audio/asound.i2s.conf" /usr/share/braillatron/audio/

# Establish a default audio output (3.5 mm aux jack) and set Speech Dispatcher's
# AudioOutputMethod to "alsa". Without this, speech-dispatcher keeps its built-in
# "pulse" default and blocks forever opening audio on headless images that have no
# PulseAudio/PipeWire server, which wedges all on-device TTS. --no-restart-ui keeps
# install.sh from starting the UI before the rest of the install completes.
"${PREFIX}/bin/braillatron-audio-select" --no-restart-ui aux || true

install -d "${CONFIG_DIR}"
install -m 644 "${DAEMON_DIR}/config/hardware.conf" "${CONFIG_DIR}/hardware.conf"
install -m 644 "${DAEMON_DIR}/config/keyboard.conf" "${CONFIG_DIR}/keyboard.conf"
install -m 644 "${DAEMON_DIR}/config/telemetry.conf" "${CONFIG_DIR}/telemetry.conf"
install -m 644 "${DAEMON_DIR}/config/ui.conf" "${CONFIG_DIR}/ui.conf"
install -m 644 "${DAEMON_DIR}/config/display.conf" "${CONFIG_DIR}/display.conf"
install -m 644 "${DAEMON_DIR}/config/matrix_map.conf" "${CONFIG_DIR}/matrix_map.conf"
install -m 644 "${DAEMON_DIR}/config/evdev_map.conf" "${CONFIG_DIR}/evdev_map.conf"
install -m 644 "${DAEMON_DIR}/config/kinematics.conf" "${CONFIG_DIR}/kinematics.conf"
install -m 644 "${ROOT}/deploy/config/braillatron.conf" "${CONFIG_DIR}/braillatron.conf"
install -m 644 "${ROOT}/deploy/config/library.conf" "${CONFIG_DIR}/library.conf"
install -m 644 "${ROOT}/deploy/config/worthwhile.conf" "${CONFIG_DIR}/worthwhile.conf"
install -m 644 "${ROOT}/deploy/config/localsend.conf" "${CONFIG_DIR}/localsend.conf"
install -m 644 "${ROOT}/deploy/config/connect.conf" "${CONFIG_DIR}/connect.conf"
install -m 644 "${ROOT}/deploy/config/remote-display.conf" "${CONFIG_DIR}/remote-display.conf"
install -m 644 "${ROOT}/deploy/config/youtube.conf" "${CONFIG_DIR}/youtube.conf"
install -m 644 "${ROOT}/deploy/config/messages.conf" "${CONFIG_DIR}/messages.conf"
install -m 644 "${ROOT}/deploy/config/dictionary.conf" "${CONFIG_DIR}/dictionary.conf"
install -m 644 "${ROOT}/deploy/config/spelling.conf" "${CONFIG_DIR}/spelling.conf"
install -m 644 "${ROOT}/deploy/config/contacts.conf" "${CONFIG_DIR}/contacts.conf"
install -m 644 "${ROOT}/deploy/config/music.conf" "${CONFIG_DIR}/music.conf"
install -m 644 "${ROOT}/deploy/config/weather.conf" "${CONFIG_DIR}/weather.conf"
install -m 644 "${ROOT}/deploy/config/podcasts.conf" "${CONFIG_DIR}/podcasts.conf"
install -m 644 "${ROOT}/deploy/config/radio.conf" "${CONFIG_DIR}/radio.conf"
install -m 644 "${ROOT}/deploy/config/gmail.conf" "${CONFIG_DIR}/gmail.conf"

install -d /usr/share/braillatron/radio
install -d /usr/share/braillatron/remote-display
install -m 644 "${ROOT}/deploy/static/remote-display/index.html" /usr/share/braillatron/remote-display/
install -m 644 "${ROOT}/deploy/static/remote-display/viewer.js" /usr/share/braillatron/remote-display/
install -m 644 "${ROOT}/deploy/radio/stations.json" /usr/share/braillatron/radio/stations.json

install -d /var/lib/braillatron/ram
install -d /data/braillatron/documents /data/braillatron/settings /data/braillatron/vosk-models || true
bash "${ROOT}/deploy/install-vosk-model.sh"
if [[ ! -f /data/braillatron/settings/remote-display.conf ]]; then
  install -m 644 "${ROOT}/deploy/config/remote-display.conf" /data/braillatron/settings/remote-display.conf
fi
install -d /data/braillatron/timer /data/braillatron/dictionary /data/braillatron/spelling-lists /data/braillatron/spelling-sessions /data/braillatron/contacts/import /data/braillatron/music /data/braillatron/weather /data/braillatron/podcasts/import /data/braillatron/podcasts/downloads /data/braillatron/radio /data/braillatron/library/books /data/braillatron/library/import /data/braillatron/library/state /data/braillatron/documents/gmail || true
if [[ -d "${ROOT}/deploy/data/contacts" ]]; then
  for seed in "${ROOT}/deploy/data/contacts/"*.csv "${ROOT}/deploy/data/contacts/"*.vcf "${ROOT}/deploy/data/contacts/"*.vcard; do
    [[ -f "${seed}" ]] || continue
    base="$(basename "${seed}")"
    if [[ ! -f "/data/braillatron/contacts/import/${base}" && ! -f "/data/braillatron/contacts/import/processed/${base}" ]]; then
      install -m 644 "${seed}" "/data/braillatron/contacts/import/${base}"
    fi
  done
fi
install -d -m 700 /data/braillatron/credentials/incoming /data/braillatron/credentials/signal-cli /data/braillatron/credentials/gmail || true

install -m 755 "${ROOT}/deploy/install-dictionary-data.sh" "${PREFIX}/bin/braillatron-install-dictionary-data"
install -m 755 "${ROOT}/deploy/install-spelling-data.sh" "${PREFIX}/bin/braillatron-install-spelling-data"
install -m 755 "${ROOT}/deploy/install-gmail-oauth.sh" "${PREFIX}/bin/braillatron-install-gmail-oauth"
install -m 755 "${ROOT}/deploy/install-vosk-lib.sh" "${PREFIX}/bin/braillatron-install-vosk-lib"
install -m 755 "${ROOT}/deploy/install-vosk-model.sh" "${PREFIX}/bin/braillatron-install-vosk-model"
install -m 755 "${ROOT}/deploy/show-pairing-code.sh" "${PREFIX}/bin/braillatron-show-pairing-code"
install -m 755 "${ROOT}/deploy/verify-install.sh" "${PREFIX}/bin/braillatron-verify-install"
install -m 755 "${ROOT}/deploy/verify-hdmi-bootstrap.sh" "${PREFIX}/bin/braillatron-verify-hdmi-bootstrap"
install -m 755 "${ROOT}/deploy/verify-display-bootstrap.sh" "${PREFIX}/bin/braillatron-verify-display-bootstrap"
bash "${ROOT}/deploy/install-dictionary-data.sh"
bash "${ROOT}/deploy/install-spelling-data.sh"
bash "${ROOT}/deploy/install-gmail-oauth.sh"

install -m 755 "${ROOT}/deploy/os/braillatron-console-ready.sh" /usr/local/sbin/braillatron-console-ready.sh
install -m 755 "${ROOT}/deploy/os/braillatron-tty1-launch.sh" /usr/local/sbin/braillatron-tty1-launch.sh
install -m 755 "${ROOT}/deploy/os/braillatron-systemd-wants.sh" /usr/local/sbin/braillatron-systemd-wants.sh
install -m 755 "${ROOT}/deploy/os/braillatron-boot-diagnose.sh" /usr/local/bin/braillatron-boot-diagnose
install -m 755 "${ROOT}/deploy/os/braillatron-fb-repaint.sh" /usr/local/sbin/braillatron-fb-repaint.sh
install -m 755 "${ROOT}/deploy/os/fix-hdmi-appliance.sh" /usr/local/sbin/fix-hdmi-appliance.sh

install -m 644 "${ROOT}/deploy/systemd/braillatron-ui.service" "${SYSTEMD_DIR}/"
install -m 644 "${ROOT}/deploy/systemd/braillatron-sentinel.service" "${SYSTEMD_DIR}/"
install -m 644 "${ROOT}/deploy/systemd/braillatron-connectd.service" "${SYSTEMD_DIR}/"
install -m 644 "${ROOT}/deploy/systemd/braillatron-displayd.service" "${SYSTEMD_DIR}/"
install -m 644 "${ROOT}/deploy/systemd/braillatron-sync.service" "${SYSTEMD_DIR}/"
install -m 644 "${ROOT}/deploy/systemd/braillatron-sync.timer" "${SYSTEMD_DIR}/"
install -m 644 "${ROOT}/deploy/systemd/braillatron-bluetooth-autoconnect.service" "${SYSTEMD_DIR}/"
install -m 644 "${ROOT}/deploy/systemd/braillatron-bluetooth-autoconnect.timer" "${SYSTEMD_DIR}/"
install -m 644 "${ROOT}/deploy/systemd/braillatron-console-ready.service" "${SYSTEMD_DIR}/"
install -m 644 "${ROOT}/deploy/systemd/braillatron-console-ui.service" "${SYSTEMD_DIR}/"
install -m 644 "${ROOT}/deploy/systemd/braillatron-fb-repaint.service" "${SYSTEMD_DIR}/"
install -m 644 "${ROOT}/deploy/systemd/braillatron-ui-stub.service" "${SYSTEMD_DIR}/"
install -m 644 "${ROOT}/deploy/systemd/braillatron.target" "${SYSTEMD_DIR}/"

systemctl daemon-reload
systemctl enable braillatron.target
systemctl enable braillatron-ui.service
systemctl disable braillatron-ui-stub.service 2>/dev/null || true
systemctl disable braillatron-console-ui.service 2>/dev/null || true
systemctl mask braillatron-console-ui.service 2>/dev/null || true
systemctl disable braillatron-console-ready.service 2>/dev/null || true
systemctl enable braillatron-fb-repaint.service 2>/dev/null || true
systemctl enable braillatron-displayd.service 2>/dev/null || true
systemctl enable braillatron-sync.timer
systemctl enable braillatron-bluetooth-autoconnect.timer
bash "${ROOT}/deploy/os/braillatron-systemd-wants.sh"

echo "Installed Braillatron to ${PREFIX}/bin and ${CONFIG_DIR}"
echo "Verify with: braillatron-verify-install"
echo "Start with: systemctl start braillatron.target"
