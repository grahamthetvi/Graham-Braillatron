#!/usr/bin/env bash
set -euo pipefail

PREFIX="${PREFIX:-/usr/local}"
TARGET="${PREFIX}/bin/yt-dlp"

if [[ "$(id -u)" -ne 0 ]]; then
  echo "install-yt-dlp.sh must run as root (sudo)." >&2
  exit 1
fi

echo "Installing latest yt-dlp to ${TARGET}..."
curl -fsSL -o "${TARGET}.new" https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp
chmod a+rx "${TARGET}.new"
mv -f "${TARGET}.new" "${TARGET}"
"${TARGET}" --version
