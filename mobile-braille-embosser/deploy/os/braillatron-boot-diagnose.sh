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
ls -l /dev/spidev3.0 2>/dev/null || echo '/dev/spidev3.0: absent'
ls -l /dev/gpiochip4 2>/dev/null || echo '/dev/gpiochip4: absent'
if [[ -f /etc/braillatron/display.conf ]]; then
  grep -E '^(backend|fbdev|hdmi_enabled|remote_display_enabled|gpio_chip|gpio_dc|gpio_rst|spidev)=' /etc/braillatron/display.conf || true
fi

section 'Remote display (displayd)'
if [[ -f /data/braillatron/settings/remote-display.conf ]]; then
  grep -E '^(enabled|allow_lan|listen_port|listen_address)=' /data/braillatron/settings/remote-display.conf || true
elif [[ -f /etc/braillatron/remote-display.conf ]]; then
  grep -E '^(enabled|allow_lan|listen_port|listen_address)=' /etc/braillatron/remote-display.conf || true
fi
systemctl is-enabled braillatron-displayd.service 2>/dev/null || echo 'displayd: not enabled'
systemctl is-active braillatron-displayd.service 2>/dev/null || echo 'displayd: not active'
journalctl -u braillatron-displayd -b -o cat 2>/dev/null | tail -5 || true

section 'Display backend (current boot journal)'
backend_line="$(journalctl -u braillatron-ui -b -o cat 2>/dev/null \
  | grep '\[display\] backend=' | tail -1 || true)"
if [[ -n "${backend_line}" ]]; then
  echo "${backend_line}"
  backend="${backend_line##*backend=}"
  case "${backend}" in
    stub|none|'')
      echo 'WARN  UI has no local visual backend'
      echo '  HDMI: confirm hdmi_enabled=true and /dev/fb0, then sudo fix-hdmi-appliance.sh && sudo reboot'
      echo '  Assistant browser: Settings → Remote display, then http://<pi-ip>:8080'
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
  [[ -f "${boot_env}" ]] || continue
  grep -E '^(overlays|extraargs)=' "${boot_env}" || true
  if grep -q 'consoleblank=0' "${boot_env}"; then
    echo "OK  ${boot_env} consoleblank=0"
  else
    echo "WARN  ${boot_env} missing consoleblank=0 (kernel may blank HDMI); run sudo fix-hdmi-appliance.sh"
  fi
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
echo 'tty1 on success: hold only; no clear/setterm/banner (tty1 and fb0 share fbcon)'
if [[ -f /usr/local/sbin/braillatron-console-ready.sh ]] \
    && grep -vE '^[[:space:]]*#' /usr/local/sbin/braillatron-console-ready.sh \
      | grep -qE 'setterm'; then
  echo 'WARN  setterm in console-ready — can wipe fb0 after network-online; run: sudo fix-hdmi-appliance.sh'
fi
if [[ -d /etc/systemd/system/getty@tty1.service.d ]]; then
  while IFS= read -r dropin; do
    [[ -n "${dropin}" ]] || continue
    if grep -q 'network-online' "${dropin}" 2>/dev/null; then
      echo "WARN  getty drop-in waits on network-online (late tty1 wipes fb0): ${dropin}"
    fi
  done < <(find /etc/systemd/system/getty@tty1.service.d -maxdepth 1 -name '*.conf' -type f 2>/dev/null || true)
fi
console_ready_enabled="$(systemctl is-enabled braillatron-console-ready.service 2>/dev/null || echo '?')"
echo "braillatron-console-ready.service enabled=${console_ready_enabled} (disabled by design; manual banner only)"
fb_repaint_enabled="$(systemctl is-enabled braillatron-fb-repaint.service 2>/dev/null || echo '?')"
echo "braillatron-fb-repaint.service enabled=${fb_repaint_enabled} (repaints fb after network-online)"

section 'Network vs HDMI timing (wlan0 ~5min wipe)'
systemctl show getty@tty1.service -p ActiveEnterTimestamp -p After -p Before --no-pager 2>/dev/null || true
systemctl show ifup@wlan0.service -p ActiveEnterTimestamp -p Result --no-pager 2>/dev/null || true
systemctl show network-online.target -p ActiveEnterTimestamp --no-pager 2>/dev/null || true
systemctl is-enabled dietpi-wifi-monitor.service 2>/dev/null || echo 'dietpi-wifi-monitor: not installed'
journalctl -u ifup@wlan0.service -u getty@tty1.service -u braillatron-fb-repaint.service -b --no-pager -n 20 2>/dev/null || true

section 'Braillatron systemd units'
systemctl is-enabled braillatron.target 2>/dev/null || true
systemctl is-active braillatron.target 2>/dev/null || true
for unit in braillatron-ui braillatron-ui-stub braillatron-sentinel braillatron-connectd braillatron-displayd; do
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
  echo 'No local display backend. HDMI: set hdmi_enabled=true, then sudo fix-hdmi-appliance.sh && sudo reboot'
  echo 'Assistant browser: Settings → Remote display, then http://<pi-ip>:8080 (or ssh -L 8080:127.0.0.1:8080)'
else
  echo 'Local backend OK. Assistant browser: Settings → Remote display → Show pairing code'
  echo 'Blank HDMI after that: sudo fix-hdmi-appliance.sh && sudo reboot'
fi
