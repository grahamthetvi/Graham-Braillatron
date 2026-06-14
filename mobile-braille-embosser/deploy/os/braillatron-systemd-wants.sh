#!/usr/bin/env bash
# Remove patch-sd-card / manual symlinks that start Braillatron child units from
# multi-user.target. Those units declare WantedBy=braillatron.target; starting them
# from multi-user races braillatron-ui vs braillatron-ui-stub and breaks HDMI.
# Also drop braillatron-ui-stub from braillatron.target.wants (patch-sd-card parity).
set -euo pipefail

SYSTEMD_DIR="${1:-/etc/systemd/system}"
MULTI_WANTS="${SYSTEMD_DIR}/multi-user.target.wants"
BRAILLATRON_WANTS="${SYSTEMD_DIR}/braillatron.target.wants"

if [[ -d "${MULTI_WANTS}" ]]; then
  for unit in \
    braillatron-ui.service \
    braillatron-ui-stub.service \
    braillatron-sentinel.service \
    braillatron-connectd.service \
    braillatron-console-ready.service \
    braillatron-console-ui.service; do
    rm -f "${MULTI_WANTS}/${unit}"
  done
fi

if [[ -d "${BRAILLATRON_WANTS}" ]]; then
  rm -f "${BRAILLATRON_WANTS}/braillatron-ui-stub.service"
fi
