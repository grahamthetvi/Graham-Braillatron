#!/usr/bin/env bash
set -euo pipefail

# DietPi-compatible read-only root with RAM-backed volatile directories.
# Run once before enabling production RO mode, then reboot.

if [[ "$(id -u)" -ne 0 ]]; then
  echo "setup-overlay-ro.sh must run as root." >&2
  exit 1
fi

# DietPi installer already adds tmpfs lines for /tmp and /var/log. Replace any
# prior entries (DietPi or stale braillatron) with one canonical block so
# systemd-fstab-generator does not see duplicate mount points.
TMPFS_MP_RE='^[[:space:]]*tmpfs[[:space:]]+/(tmp|var/tmp|var/log|var/lib/braillatron|var/lib/dhcp)[[:space:]]'
TMPFS_HDR_RE='^# braillatron volatile tmpfs'
fstab_tmp="$(mktemp)"
grep -vE "${TMPFS_MP_RE}|${TMPFS_HDR_RE}" /etc/fstab >"${fstab_tmp}"
cat >>"${fstab_tmp}" <<'EOF'
# braillatron volatile tmpfs (setup-overlay-ro.sh)
tmpfs /tmp tmpfs size=256M,noatime,nodev,nosuid,mode=1777 0 0
tmpfs /var/tmp tmpfs size=64M,noatime,nodev,nosuid,mode=1777 0 0
tmpfs /var/log tmpfs size=50M,noatime,nodev,nosuid,mode=0755 0 0
tmpfs /var/lib/braillatron tmpfs size=128M,noatime,nodev,nosuid,mode=0755 0 0
tmpfs /var/lib/dhcp tmpfs size=4M,noatime,nodev,nosuid,mode=0755 0 0
EOF
awk '$2=="/" { gsub(/,rw/, ",ro"); gsub(/ rw /, " ro "); sub(/ rw$/, " ro"); sub(/^([^ ]+ [^ ]+ [^ ]+ )rw /, "\\1ro ") }1' "${fstab_tmp}" >"${fstab_tmp}.ro"
install -m 644 "${fstab_tmp}.ro" /etc/fstab
rm -f "${fstab_tmp}" "${fstab_tmp}.ro"

mkdir -p /var/lib/braillatron/ram

# Configure resolv.conf as a symlink to /run/resolv.conf to allow DHCP DNS writes on read-only root.
if [[ -f /etc/resolv.conf && ! -L /etc/resolv.conf ]]; then
  touch /run/resolv.conf
  cp /etc/resolv.conf /run/resolv.conf || true
  ln -sf /run/resolv.conf /etc/resolv.conf
fi

cat >/usr/local/sbin/braillatron-remount-rw <<'EOF'
#!/usr/bin/env bash
mount -o remount,rw /
EOF
chmod 755 /usr/local/sbin/braillatron-remount-rw

cat >/usr/local/sbin/braillatron-remount-ro <<'EOF'
#!/usr/bin/env bash
sync
mount -o remount,ro /
EOF
chmod 755 /usr/local/sbin/braillatron-remount-ro

echo "Volatile tmpfs mounts configured; root fstab entry set to ro."
echo "Reboot to apply tmpfs mounts and read-only root:"
echo "  sync && reboot"
