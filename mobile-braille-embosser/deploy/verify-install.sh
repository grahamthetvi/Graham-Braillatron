#!/usr/bin/env bash
set -euo pipefail

CONFIG_DIR="${CONFIG_DIR:-/etc/braillatron}"
PREFIX="${PREFIX:-/usr/local}"
failures=0

check() {
  if [[ "$1" == "ok" ]]; then
    echo "ok: $2"
  else
    echo "FAIL: $2" >&2
    failures=$((failures + 1))
  fi
}

for bin in braillatron-ui braillatron-connectd braillatron-displayd braillatron-install-dictionary-data \
           braillatron-install-spelling-data braillatron-install-gmail-oauth; do
  [[ -x "${PREFIX}/bin/${bin}" ]] && check ok "binary ${bin}" || check fail "binary ${bin}"
done

for conf in connect.conf youtube.conf messages.conf dictionary.conf spelling.conf contacts.conf \
            music.conf weather.conf podcasts.conf radio.conf gmail.conf library.conf; do
  [[ -f "${CONFIG_DIR}/${conf}" ]] && check ok "config ${conf}" || check fail "config ${conf}"
done

for dir in /data/braillatron/timer /data/braillatron/dictionary /data/braillatron/spelling-lists \
           /data/braillatron/contacts/import /data/braillatron/music /data/braillatron/weather \
           /data/braillatron/podcasts/downloads /data/braillatron/radio /data/braillatron/library/books \
           /data/braillatron/credentials/incoming /data/braillatron/credentials/gmail; do
  [[ -d "${dir}" ]] && check ok "data dir ${dir}" || check fail "data dir ${dir}"
done

[[ -f /usr/share/braillatron/radio/stations.json ]] && check ok "radio stations bundle" \
  || check fail "radio stations bundle"

if command -v systemctl >/dev/null 2>&1; then
  systemctl is-enabled braillatron.target >/dev/null 2>&1 && check ok "braillatron.target enabled" \
    || check fail "braillatron.target enabled"
fi

if [[ "${failures}" -eq 0 ]]; then
  echo "verify-install: all checks passed"
  exit 0
fi

echo "verify-install: ${failures} check(s) failed" >&2
exit 1
