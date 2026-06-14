#!/usr/bin/env bash
# Fail bootstrap when known HDMI regressions are still present on disk.
set -euo pipefail

PREFIX="${PREFIX:-/usr/local}"
SYSTEMD_DIR="/etc/systemd/system"
failures=0

check() {
  if [[ "$1" == "ok" ]]; then
    echo "ok: $2"
  else
    echo "FAIL: $2" >&2
    failures=$((failures + 1))
  fi
}

console_ready="${PREFIX}/sbin/braillatron-console-ready.sh"
tty1_launch="${PREFIX}/sbin/braillatron-tty1-launch.sh"

if [[ -f "${console_ready}" ]] \
    && grep -qE 'setterm.*-blank[[:space:]=]+force|-blank[[:space:]]+force' "${console_ready}"; then
  check fail "console-ready uses setterm -blank force (blanks HDMI framebuffer)"
elif [[ -f "${console_ready}" ]] \
    && grep -qE 'setterm.*-blank[[:space:]=]+0|-blank[[:space:]]+0' "${console_ready}"; then
  check ok "console-ready disables VT blanking (setterm -blank 0)"
else
  check fail "console-ready missing setterm -blank 0 (HDMI may blank)"
fi

if [[ -f "${tty1_launch}" ]]; then
  if tail -n 8 "${tty1_launch}" | grep -q '^clear_tty1$'; then
    check fail "tty1 launch still calls clear_tty1 on HDMI success (wipes /dev/fb0)"
  elif tail -n 8 "${tty1_launch}" | grep -q 'blank_tty1_cursor'; then
    check ok "tty1 launch blanks cursor without clear_tty1 on HDMI success"
  else
    check fail "tty1 launch missing blank_tty1_cursor on HDMI success path"
  fi
else
  check fail "tty1 launch script missing"
fi

if command -v systemctl >/dev/null 2>&1; then
  if systemctl is-enabled braillatron-ui-stub.service >/dev/null 2>&1; then
    check fail "braillatron-ui-stub.service is enabled (conflicts with braillatron-ui on HDMI)"
  else
    check ok "braillatron-ui-stub.service disabled"
  fi
  systemctl is-enabled braillatron-ui.service >/dev/null 2>&1 \
    && check ok "braillatron-ui.service enabled" \
    || check fail "braillatron-ui.service enabled"
fi

target_file="${SYSTEMD_DIR}/braillatron.target"
if [[ -f "${target_file}" ]] && grep -q 'braillatron-ui-stub' "${target_file}"; then
  check fail "braillatron.target still Wants braillatron-ui-stub.service"
else
  check ok "braillatron.target has no stub Wants="
fi

if [[ -f /etc/braillatron/appliance-headless ]]; then
  check fail "appliance-headless flag present (TTS-only, no HDMI UI)"
else
  check ok "appliance-headless absent"
fi

if [[ -f /etc/braillatron/ui.conf ]]; then
  grep -q '^tts_enabled=true' /etc/braillatron/ui.conf \
    && check ok "ui.conf tts_enabled=true" \
    || check fail "ui.conf tts_enabled not true"
  grep -q '^stt_enabled=true' /etc/braillatron/ui.conf \
    && check ok "ui.conf stt_enabled=true" \
    || check fail "ui.conf stt_enabled not true"
fi

getty_dropin="${SYSTEMD_DIR}/getty@tty1.service.d/braillatron-appliance.conf"
[[ -f "${getty_dropin}" ]] && check ok "getty@tty1 braillatron drop-in" \
  || check fail "getty@tty1 braillatron drop-in missing"

if [[ -f /etc/braillatron/display.conf ]]; then
  grep -q '^hdmi_enabled=true' /etc/braillatron/display.conf \
    && check ok "display.conf hdmi_enabled=true" \
    || check fail "display.conf hdmi_enabled not true"
fi

if [[ "${failures}" -eq 0 ]]; then
  echo "verify-hdmi-bootstrap: all checks passed"
  exit 0
fi

echo "verify-hdmi-bootstrap: ${failures} check(s) failed — HDMI will be blank until fixed" >&2
echo "  On Pi: sudo fix-hdmi-appliance.sh && sudo reboot" >&2
exit 1
