#!/usr/bin/env bash
set -euo pipefail

# Downloads the default English Vosk STT model to /data (not in Debian apt).
VOSK_MODEL="${VOSK_MODEL:-vosk-model-small-en-us-0.15}"
VOSK_MODEL_URL="${VOSK_MODEL_URL:-https://alphacephei.com/vosk/models/${VOSK_MODEL}.zip}"
MODEL_ROOT="${VOSK_MODEL_ROOT:-/data/braillatron/vosk-models}"
MODEL_DIR="${MODEL_ROOT}/${VOSK_MODEL}"

if [[ "$(id -u)" -ne 0 ]]; then
  echo "install-vosk-model.sh must run as root (sudo)." >&2
  exit 1
fi

if [[ -d "${MODEL_DIR}" ]]; then
  echo "Vosk model already installed at ${MODEL_DIR}"
  exit 0
fi

if ! command -v curl >/dev/null 2>&1 || ! command -v unzip >/dev/null 2>&1; then
  echo "install-vosk-model.sh requires curl and unzip." >&2
  exit 1
fi

install -d "${MODEL_ROOT}"
echo "Downloading Vosk model ${VOSK_MODEL} (~40 MB)..."
tmp="$(mktemp -d)"
trap 'rm -rf "${tmp}"' EXIT
curl -fsSL -o "${tmp}/vosk-model.zip" "${VOSK_MODEL_URL}"
unzip -q "${tmp}/vosk-model.zip" -d "${MODEL_ROOT}/"
echo "Vosk model installed at ${MODEL_DIR}"
