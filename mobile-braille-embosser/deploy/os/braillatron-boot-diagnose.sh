#!/usr/bin/env bash
# Quick boot-path diagnostics for a Braillatron Pi (run over SSH).
set -euo pipefail

section() {
  printf '\n== %s ==\n' "$1"
}

section 'Display routing flags'
test -f /etc/braillatron/appliance-spi && echo 'appliance-spi: present (SPI HAT configured)' || echo 'appliance-spi: absent'
test -f /etc/braillatron/appliance-headless && echo 'appliance-headless: present (TTS-only)' || echo 'appliance-headless: absent'
cat /etc/braillatron/appliance.env 2>/dev/null || echo 'appliance.env: missing'

section 'Display devices'
ls -l /dev/fb0 2>/dev/null || echo '/dev/fb0: absent'
ls -l /dev/spidev0.0 2>/dev/null || echo '/dev/spidev0.0: absent'
if [[ -f /etc/braillatron/display.conf ]]; then
  grep -E '^(backend|fbdev|hdmi_enabled|gpio_dc|spidev)=' /etc/braillatron/display.conf || true
fi

section 'Display backend (current boot journal)'
backend_line="$(journalctl -u braillatron-ui -b -o cat 2>/dev/null \
  | grep '\[display\] backend=' | tail -1 || true)"
if [[ -n "${backend_line}" ]]; then
  echo "${backend_line}"
  backend="${backend_line##*backend=}"
  case "${backend}" in
    stub|none|'')
      echo 'WARN  UI has no visual display backend'
      echo '  Check /dev/fb0, display.conf hdmi_enabled=true, braillatron-ui video group'
      ;;
    fb|spi|spi+fb|fb+spi)
      echo "OK  visual backend=${backend}"
      ;;
    *)
      echo "NOTE  backend=${backend}"
      ;;
  esac
else
  echo 'No [display] backend= line in braillatron-ui journal this boot'
fi

section 'Kernel / boot overlays'
for boot_env in /boot/dietpiEnv.txt /boot/firmware/dietpiEnv.txt; do
  [[ -f "${boot_env}" ]] && grep -E '^(overlays|extraargs)=' "${boot_env}" || true
done

section 'Binaries and console tools'
for bin in /usr/local/bin/braillatron-ui /usr/local/sbin/braillatron-tty1-launch.sh \
           /usr/local/sbin/braillatron-console-ready.sh /usr/bin/figlet; do
  if [[ -x "${bin}" ]]; then
    echo "OK  ${bin}"
  elif [[ -f "${bin}" ]]; then
    echo "NOT EXECUTABLE  ${bin}"
  else
    echo "MISSING  ${bin}"
  fi
done

section 'getty@tty1 (UI-first console)'
systemctl is-enabled getty@tty1.service 2>/dev/null || true
systemctl is-active getty@tty1.service 2>/dev/null || true
if [[ -f /etc/systemd/system/getty@tty1.service.d/braillatron-appliance.conf ]]; then
  echo 'OK  braillatron-appliance getty drop-in installed'
else
  echo 'MISSING  getty drop-in'
fi
echo 'tty1 on success: blank cursor only; framebuffer UI must not be cleared (no ESC [2J)'
if [[ -f /usr/local/sbin/braillatron-console-ready.sh ]] \
    && grep -qE 'setterm.*-blank[[:space:]=]+force|-blank[[:space:]]+force' \
      /usr/local/sbin/braillatron-console-ready.sh; then
  echo 'WARN  setterm -blank force in console-ready — blanks HDMI; run: sudo fix-hdmi-appliance.sh'
fi
console_ready_enabled="$(systemctl is-enabled braillatron-console-ready.service 2>/dev/null || echo '?')"
echo "braillatron-console-ready.service enabled=${console_ready_enabled} (disabled by design; manual banner only)"

section 'Braillatron systemd units'
systemctl is-enabled braillatron.target 2>/dev/null || true
systemctl is-active braillatron.target 2>/dev/null || true
for unit in braillatron-ui braillatron-ui-stub braillatron-sentinel braillatron-connectd; do
  printf '%-24s enabled=%-8s active=%-8s condition=%s\n' "${unit}" \
    "$(systemctl is-enabled "${unit}.service" 2>/dev/null || echo '?')" \
    "$(systemctl is-active "${unit}.service" 2>/dev/null || echo '?')" \
    "$(systemctl show "${unit}.service" -p ConditionResult --value 2>/dev/null || echo '?')"
done

section 'Recent UI journal'
journalctl -u braillatron-ui.service -u getty@tty1.service -b --no-pager -n 30 2>/dev/null || true

section 'Suggested next step'
if [[ ! -x /usr/local/bin/braillatron-ui ]]; then
  echo 'Run full bootstrap: sudo bash deploy/bootstrap-dietpi.sh && sudo reboot'
elif [[ ! -f /etc/systemd/system/getty@tty1.service.d/braillatron-appliance.conf ]]; then
  echo 'Re-apply appliance mode: sudo bash deploy/os/setup-appliance-mode.sh && sudo reboot'
elif [[ "${backend_line}" == *'backend=stub'* ]] || [[ -z "${backend_line}" ]]; then
  echo 'Stub or missing display backend: check /dev/fb0, display.conf hdmi_enabled=true, video group on braillatron-ui.service'
else
  echo 'If HDMI is blank: sudo fix-hdmi-appliance.sh && sudo reboot'
  echo '  (backend=fb OK but blank usually means setterm -blank force or missing post-bootstrap reboot)'
fi
