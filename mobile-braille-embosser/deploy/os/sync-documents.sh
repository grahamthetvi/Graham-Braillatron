#!/usr/bin/env bash
set -euo pipefail

CONFIG_DIR="${BRAILLATRON_CONFIG:-/etc/braillatron}"
TELEMETRY_CONF="${CONFIG_DIR}/telemetry.conf"

read_kv() {
  local key="$1"
  local file="$2"
  grep -E "^${key}=" "${file}" | head -n1 | cut -d= -f2-
}

RAM_LAYERS="$(read_kv ram_text_layers "${TELEMETRY_CONF}")"
DEST_DIR="$(read_kv persistent_output_dir "${TELEMETRY_CONF}")"

if [[ -z "${RAM_LAYERS}" || -z "${DEST_DIR}" ]]; then
  echo "braillatron-sync: missing ram_text_layers or persistent_output_dir" >&2
  exit 1
fi

mkdir -p "${DEST_DIR}"

IFS=',' read -r -a layers <<< "${RAM_LAYERS}"
index=0
for layer in "${layers[@]}"; do
  layer="$(echo "${layer}" | xargs)"
  if [[ -f "${layer}" ]]; then
    tmp="${DEST_DIR}/layer${index}.brf.tmp"
    dest="${DEST_DIR}/layer${index}.brf"
    cp "${layer}" "${tmp}"
    sync "${tmp}"
    mv "${tmp}" "${dest}"
    sync "${dest}"
  fi
  index=$((index + 1))
done

echo "braillatron-sync: persisted ${index} layer(s) to ${DEST_DIR}"
