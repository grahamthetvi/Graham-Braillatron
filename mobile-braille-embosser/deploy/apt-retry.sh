#!/usr/bin/env bash
# Resilient apt-get wrappers for bootstrap on flaky Wi-Fi / large package pulls.
# Source from deploy scripts:  source "$(dirname ...)/apt-retry.sh"

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
  echo "apt-retry.sh is a library; source it from another deploy script." >&2
  exit 1
fi

_APT_RETRY_MAX="${BRAILLATRON_APT_RETRIES:-5}"
_APT_RETRY_DELAY="${BRAILLATRON_APT_RETRY_DELAY:-10}"
_APT_COMMON_OPTS=(
  -o Acquire::Retries=5
  -o Acquire::http::Timeout=120
  -o Acquire::https::Timeout=120
)

apt_retry_update() {
  local attempt
  for ((attempt = 1; attempt <= _APT_RETRY_MAX; attempt++)); do
    if apt-get "${_APT_COMMON_OPTS[@]}" update; then
      return 0
    fi
    if (( attempt < _APT_RETRY_MAX )); then
      echo "apt-get update failed (attempt ${attempt}/${_APT_RETRY_MAX}); retrying in ${_APT_RETRY_DELAY}s..." >&2
      sleep "${_APT_RETRY_DELAY}"
    fi
  done
  echo "apt-get update failed after ${_APT_RETRY_MAX} attempts." >&2
  return 1
}

apt_retry_install() {
  local attempt extra=()
  for ((attempt = 1; attempt <= _APT_RETRY_MAX; attempt++)); do
    if (( attempt > 1 )); then
      extra=(--fix-missing)
      apt_retry_update || true
    fi
    if apt-get "${_APT_COMMON_OPTS[@]}" install -y "${extra[@]}" "$@"; then
      return 0
    fi
    if (( attempt < _APT_RETRY_MAX )); then
      echo "apt-get install failed (attempt ${attempt}/${_APT_RETRY_MAX}); retrying in ${_APT_RETRY_DELAY}s..." >&2
      sleep "${_APT_RETRY_DELAY}"
    fi
  done
  echo "apt-get install failed after ${_APT_RETRY_MAX} attempts." >&2
  return 1
}
