#!/usr/bin/env bash
set -euo pipefail

# Installs prebuilt libvosk for Orange Pi 3B (aarch64). Not in Debian apt.
VOSK_VERSION="${VOSK_VERSION:-0.3.45}"
PREFIX="${PREFIX:-/usr/local}"

if [[ "$(id -u)" -ne 0 ]]; then
  echo "install-vosk-lib.sh must run as root (sudo)." >&2
  exit 1
fi

if [[ "$(uname -m)" != "aarch64" ]]; then
  echo "Expected aarch64 (Orange Pi 3B); got $(uname -m)." >&2
  exit 1
fi

if [[ -f "${PREFIX}/lib/libvosk.so" ]]; then
  echo "libvosk already installed at ${PREFIX}/lib/libvosk.so"
  exit 0
fi

tmp="$(mktemp -d)"
trap 'rm -rf "${tmp}"' EXIT

archive="vosk-linux-aarch64-${VOSK_VERSION}.zip"
url="https://github.com/alphacep/vosk-api/releases/download/v${VOSK_VERSION}/${archive}"

echo "Downloading ${url}..."
curl -fsSL -o "${tmp}/${archive}" "${url}"
unzip -q "${tmp}/${archive}" -d "${tmp}"

install -d "${PREFIX}/lib" "${PREFIX}/include"
install -m 644 "${tmp}/vosk-linux-aarch64-${VOSK_VERSION}/libvosk.so" "${PREFIX}/lib/"
install -m 644 "${tmp}/vosk-linux-aarch64-${VOSK_VERSION}/vosk_api.h" "${PREFIX}/include/"
ldconfig

echo "libvosk ${VOSK_VERSION} installed to ${PREFIX}"
