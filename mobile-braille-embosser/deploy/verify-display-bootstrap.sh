#!/usr/bin/env bash
# Default bootstrap verification for wireless-first appliance display.
set -euo pipefail

PREFIX="${PREFIX:-/usr/local}"
SYSTEMD_DIR="/etc/systemd/system"
failures=0
HDMI_MODE=0

if [[ "${BRAILLATRON_HDMI:-0}" == "1" ]]; then
  HDMI_MODE=1
elif [[ -f /etc/braillatron/display.conf ]] && grep -q '^hdmi_enabled=true' /etc/braillatron/display.conf; then
  HDMI_MODE=1
fi

check() {
  if [[ "$1" == "ok" ]]; then
    echo "ok: $2"
  else
    echo "FAIL: $2" >&2
    failures=$((failures + 1))
  fi
}

if [[ -f /etc/braillatron/display.conf ]]; then
  if grep -q '^hdmi_enabled=false' /etc/braillatron/display.conf; then
    check ok "display.conf hdmi_enabled=false (wireless-first default)"
  else
    check fail "display.conf hdmi_enabled should be false unless BRAILLATRON_HDMI=1"
  fi
  grep -q '^remote_display_socket=' /etc/braillatron/display.conf \
    && check ok "display.conf remote_display_socket configured" \
    || check fail "display.conf missing remote_display_socket"
fi

if [[ -f /data/braillatron/settings/remote-display.conf ]]; then
  check ok "remote-display settings directory in use"
else
  check ok "remote-display settings will use /etc/braillatron/remote-display.conf until copied"
fi

if command -v systemctl >/dev/null 2>&1; then
  systemctl is-enabled braillatron-displayd.service >/dev/null 2>&1 \
    && check ok "braillatron-displayd.service enabled" \
    || check fail "braillatron-displayd.service enabled"
  systemctl is-enabled braillatron-ui.service >/dev/null 2>&1 \
    && check ok "braillatron-ui.service enabled" \
    || check fail "braillatron-ui.service enabled"
fi

target_file="${SYSTEMD_DIR}/braillatron.target"
if [[ -f "${target_file}" ]] && grep -q 'braillatron-displayd.service' "${target_file}"; then
  check ok "braillatron.target Wants displayd"
else
  check fail "braillatron.target missing braillatron-displayd.service"
fi

if [[ "${HDMI_MODE}" -eq 1 ]]; then
  echo "HDMI opt-in mode: running HDMI bootstrap checks..."
  if [[ -x "${PREFIX}/bin/braillatron-verify-hdmi-bootstrap" ]]; then
    "${PREFIX}/bin/braillatron-verify-hdmi-bootstrap" || failures=$((failures + 1))
  else
    check fail "braillatron-verify-hdmi-bootstrap missing for HDMI mode"
  fi
fi

if [[ "${failures}" -eq 0 ]]; then
  echo "verify-display-bootstrap: all checks passed"
  exit 0
fi

echo "verify-display-bootstrap: ${failures} check(s) failed" >&2
echo "  Bench without SPI: enable Remote display in Settings, pair at http://<pi-ip>:8080" >&2
echo "  LAN disabled: ssh -L 8080:127.0.0.1:8080 user@<pi-ip> then open http://localhost:8080" >&2
exit 1
