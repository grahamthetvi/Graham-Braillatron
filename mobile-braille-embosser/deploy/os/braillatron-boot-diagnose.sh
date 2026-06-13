#!/usr/bin/env bash
# Quick boot-path diagnostics for a Braillatron Pi (run over SSH).
set -euo pipefail

section() {
  printf '\n== %s ==\n' "$1"
}

section 'Display routing flags'
test -f /etc/braillatron/appliance-spi && echo 'appliance-spi: present (SPI panel path)' || echo 'appliance-spi: absent (HDMI path)'
test -f /etc/braillatron/appliance-headless && echo 'appliance-headless: present (TTS-only)' || echo 'appliance-headless: absent'
cat /etc/braillatron/appliance.env 2>/dev/null || echo 'appliance.env: missing'

section 'Kernel / boot overlays'
ls -l /dev/spidev0.0 2>/dev/null || echo '/dev/spidev0.0: absent'
for boot_env in /boot/dietpiEnv.txt /boot/firmware/dietpiEnv.txt; do
  [[ -f "${boot_env}" ]] && grep -E '^(overlays|extraargs)=' "${boot_env}" || true
done

section 'Binaries and console tools'
for bin in /usr/local/bin/braillatron-ui /usr/local/sbin/braillatron-tty1-launch.sh \
           /usr/local/sbin/braillatron-console-ready.sh /usr/bin/openvt /usr/bin/figlet; do
  if [[ -x "${bin}" ]]; then
    echo "OK  ${bin}"
  elif [[ -f "${bin}" ]]; then
    echo "NOT EXECUTABLE  ${bin}"
  else
    echo "MISSING  ${bin}"
  fi
done

section 'getty@tty1 (HDMI owner)'
systemctl is-enabled getty@tty1.service 2>/dev/null || true
systemctl is-active getty@tty1.service 2>/dev/null || true
if [[ -f /etc/systemd/system/getty@tty1.service.d/braillatron-appliance.conf ]]; then
  echo 'OK  braillatron-appliance getty drop-in installed'
else
  echo 'MISSING  getty drop-in — appliance mode may still use masked getty + openvt'
fi
if [[ -L /etc/systemd/system/getty@tty1.service ]] && [[ "$(readlink /etc/systemd/system/getty@tty1.service)" == "/dev/null" ]]; then
  echo 'WARN  getty@tty1 is masked — HDMI stays blank unless console-ui draws'
fi

section 'Braillatron systemd units'
systemctl is-enabled braillatron.target 2>/dev/null || true
systemctl is-active braillatron.target 2>/dev/null || true
for unit in braillatron-console-ui braillatron-ui braillatron-ui-stub braillatron-sentinel braillatron-connectd; do
  printf '%-24s enabled=%-8s active=%-8s condition=%s\n' "${unit}" \
    "$(systemctl is-enabled "${unit}.service" 2>/dev/null || echo '?')" \
    "$(systemctl is-active "${unit}.service" 2>/dev/null || echo '?')" \
    "$(systemctl show "${unit}.service" -p ConditionResult --value 2>/dev/null || echo '?')"
done

section 'Recent console / UI journal'
journalctl -u getty@tty1.service -u braillatron-console-ui.service -u braillatron-ui.service -b --no-pager -n 25 2>/dev/null || true

section 'Suggested next step'
if [[ ! -x /usr/local/bin/braillatron-ui ]]; then
  echo 'Run full bootstrap: sudo bash deploy/bootstrap-dietpi.sh && sudo reboot'
elif [[ ! -f /etc/systemd/system/getty@tty1.service.d/braillatron-appliance.conf ]]; then
  echo 'Re-apply appliance mode: sudo bash deploy/os/setup-appliance-mode.sh && sudo reboot'
else
  echo 'If HDMI is still blank: journalctl -u getty@tty1.service -b --no-pager | tail -40'
fi
