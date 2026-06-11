#!/usr/bin/env bash
set -euo pipefail

PREFIX="${PREFIX:-/usr/local}"
ARCH="$(uname -m)"
VERSION="${SIGNAL_CLI_VERSION:-0.13.14}"
URL="https://github.com/AsamK/signal-cli/releases/download/v${VERSION}/signal-cli-${VERSION}-Linux-native.tar.gz"
DATA_DIR="/data/braillatron/credentials/signal-cli"

if [[ "$(id -u)" -ne 0 ]]; then
  echo "install-signal-cli.sh must run as root (sudo)." >&2
  exit 1
fi

case "${ARCH}" in
  aarch64|arm64) ;;
  x86_64|amd64) ;;
  *)
    echo "Unsupported architecture for signal-cli native install: ${ARCH}" >&2
    exit 1
    ;;
esac

tmp="$(mktemp -d)"
trap 'rm -rf "${tmp}"' EXIT

echo "Downloading signal-cli ${VERSION}..."
curl -fsSL -o "${tmp}/signal-cli.tar.gz" "${URL}"
tar -xf "${tmp}/signal-cli.tar.gz" -C "${tmp}"

install -d "${PREFIX}/bin"
install -m 755 "${tmp}/signal-cli" "${PREFIX}/bin/signal-cli"

install -d -m 700 "${DATA_DIR}" || true

echo "Installed signal-cli to ${PREFIX}/bin/signal-cli"
