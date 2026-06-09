#!/usr/bin/env bash
set -euo pipefail

# DietPi-compatible read-only root with RAM-backed volatile directories.
# Run once before enabling production RO mode, then reboot.

if [[ "$(id -u)" -ne 0 ]]; then
  echo "setup-overlay-ro.sh must run as root." >&2
  exit 1
fi

OVERLAY_DIR="/opt/braillatron-overlay"
mkdir -p "${OVERLAY_DIR}"/{upper,work,var/log,var/tmp,tmp}

# Guard string must match a line actually written below, or re-runs append
# duplicate tmpfs entries forever.
if ! grep -q '^# braillatron volatile tmpfs' /etc/fstab; then
  cat >> /etc/fstab <<'EOF'
# braillatron volatile tmpfs (setup-overlay-ro.sh)
tmpfs /tmp tmpfs defaults,noatime,mode=1777 0 0
tmpfs /var/tmp tmpfs defaults,noatime,mode=1777 0 0
tmpfs /var/log tmpfs defaults,noatime,mode=0755 0 0
EOF
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

echo "Overlay volatile mounts configured."
echo "To lock root read-only after validation:"
echo "  sync && mount -o remount,ro /"
