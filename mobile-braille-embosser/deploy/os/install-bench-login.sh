#!/usr/bin/env bash
# Install or remove a login-shell hook that auto-starts braillatron-ui on local
# virtual consoles (tty1, tty2, …). SSH sessions are left alone for dev work.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
CONSOLE_SCRIPT="${SCRIPT_DIR}/braillatron-bench-console.sh"
MARK_BEGIN="# >>> braillatron-bench-login >>>"
MARK_END="# <<< braillatron-bench-login <<<"
PROFILE="${HOME}/.bash_profile"

usage() {
  cat <<EOF
Usage: $(basename "$0") [--uninstall]

Installs a ~/.bash_profile hook that runs braillatron-ui (ncurses display build)
after login on a local virtual console. SSH logins are not affected.

Disable once:  BRAILLATRON_BENCH_AUTO=0 login
Disable hook:  $(basename "$0") --uninstall
EOF
}

write_snippet() {
  cat <<EOF
${MARK_BEGIN}
if [[ -z "\${SSH_CONNECTION:-}" && -z "\${SSH_TTY:-}" && "\${BRAILLATRON_BENCH_AUTO:-1}" == "1" ]]; then
  case "\$(tty)" in
    /dev/tty[0-9]*)
      if [[ -x "${CONSOLE_SCRIPT}" ]]; then
        "${CONSOLE_SCRIPT}"
      fi
      ;;
  esac
fi
${MARK_END}
EOF
}

remove_snippet() {
  if [[ ! -f "${PROFILE}" ]]; then
    return 0
  fi
  awk -v begin="${MARK_BEGIN}" -v end="${MARK_END}" '
    $0 == begin { skip=1; next }
    $0 == end { skip=0; next }
    !skip { print }
  ' "${PROFILE}" > "${PROFILE}.tmp"
  mv "${PROFILE}.tmp" "${PROFILE}"
}

install_snippet() {
  if [[ -f "${PROFILE}" ]] && grep -qF "${MARK_BEGIN}" "${PROFILE}"; then
    remove_snippet
  fi

  if [[ ! -f "${PROFILE}" ]]; then
    touch "${PROFILE}"
  fi

  {
    echo
    write_snippet
  } >> "${PROFILE}"
}

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  usage
  exit 0
fi

if [[ "${1:-}" == "--uninstall" ]]; then
  remove_snippet
  echo "Removed Braillatron bench login hook from ${PROFILE}"
  exit 0
fi

if [[ "${1:-}" != "" ]]; then
  echo "Unknown option: $1" >&2
  usage >&2
  exit 1
fi

chmod +x "${CONSOLE_SCRIPT}"
install_snippet

echo "Installed Braillatron bench login hook in ${PROFILE}"
echo "  Local console (tty): auto-builds display target and starts braillatron-ui"
echo "  SSH: normal shell for development"
echo "  Skip once: BRAILLATRON_BENCH_AUTO=0 login"
echo "  Remove:    ${SCRIPT_DIR}/$(basename "$0") --uninstall"
