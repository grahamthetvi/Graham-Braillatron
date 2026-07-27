#!/usr/bin/env bash
# Ensure /data/braillatron/dictionary/en.sqlite is populated.
# Preference order:
#   1. Already-complete DB (>= MIN_ENTRIES)
#   2. Packaged offline DB shipped with the image (/usr/share/...)
#   3. Stream-import from Kaikki (network) with retries
#   4. Tiny seed stub (last resort; next boot will retry)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IMPORT_SCRIPT="${SCRIPT_DIR}/braillatron-import-kaikki-dictionary"
if [[ ! -x "${IMPORT_SCRIPT}" ]]; then
  IMPORT_SCRIPT="${ROOT}/deploy/import-kaikki-dictionary.py"
fi
if [[ ! -f "${IMPORT_SCRIPT}" ]]; then
  IMPORT_SCRIPT="/usr/local/bin/braillatron-import-kaikki-dictionary"
fi

DB_PATH="${DB_PATH:-/data/braillatron/dictionary/en.sqlite}"
PACKAGED_DB="${PACKAGED_DB:-/usr/share/braillatron/dictionary/en.sqlite}"
KAIKKI_JSONL="${KAIKKI_JSONL:-}"
KAIKKI_URL="${KAIKKI_URL:-https://kaikki.org/dictionary/English/kaikki.org-dictionary-English.jsonl}"
MIN_ENTRIES="${MIN_ENTRIES:-10000}"
MAX_IMPORT_ROWS="${MAX_IMPORT_ROWS:-500000}"
DOWNLOAD_RETRIES="${DOWNLOAD_RETRIES:-3}"
NETWORK_WAIT_SECS="${NETWORK_WAIT_SECS:-120}"

if [[ "$(id -u)" -ne 0 && "${DB_PATH}" == /data/* ]]; then
  echo "install-dictionary-data.sh must run as root for ${DB_PATH} (sudo)." >&2
  exit 1
fi

install -d "$(dirname "${DB_PATH}")" 2>/dev/null || mkdir -p "$(dirname "${DB_PATH}")"

existing_entry_count() {
  if [[ ! -f "${DB_PATH}" ]]; then
    echo 0
    return
  fi
  sqlite3 "${DB_PATH}" "SELECT COUNT(*) FROM entries;" 2>/dev/null || echo 0
}

wait_for_network() {
  local deadline=$((SECONDS + NETWORK_WAIT_SECS))
  while (( SECONDS < deadline )); do
    if curl -fsI -m 5 "${KAIKKI_URL}" >/dev/null 2>&1; then
      return 0
    fi
    if ping -c1 -W2 1.1.1.1 >/dev/null 2>&1 || ping -c1 -W2 8.8.8.8 >/dev/null 2>&1; then
      # IP up but Kaikki HEAD failed — still try the download.
      return 0
    fi
    sleep 2
  done
  return 1
}

install_packaged_db() {
  if [[ ! -f "${PACKAGED_DB}" ]]; then
    return 1
  fi
  local packaged_count
  packaged_count="$(sqlite3 "${PACKAGED_DB}" "SELECT COUNT(*) FROM entries;" 2>/dev/null || echo 0)"
  if [[ "${packaged_count}" -lt "${MIN_ENTRIES}" ]]; then
    return 1
  fi
  echo "Installing packaged dictionary (${packaged_count} rows) from ${PACKAGED_DB}"
  install -m 644 "${PACKAGED_DB}" "${DB_PATH}.tmp"
  mv -f "${DB_PATH}.tmp" "${DB_PATH}"
  return 0
}

build_seed_db() {
  sqlite3 "${DB_PATH}" <<'SQL'
PRAGMA journal_mode=WAL;
CREATE TABLE IF NOT EXISTS entries (
  word TEXT NOT NULL,
  pos TEXT,
  definition TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_entries_word ON entries(word COLLATE NOCASE);
DELETE FROM entries;
INSERT INTO entries VALUES
  ('hello','interjection','Used as a greeting or to begin a phone conversation.'),
  ('hello','noun','An utterance of hello; a greeting.'),
  ('world','noun','The earth, together with all of its countries and peoples.'),
  ('cent','noun','A monetary unit equal to one hundredth of a dollar or euro.'),
  ('braillatron','noun','A mobile smart braille notetaker and embosser.');
SQL
}

import_kaikki_jsonl() {
  local source="$1"
  if [[ "${source}" != "-" && ! -f "${source}" ]]; then
    echo "Kaikki JSONL not found: ${source}" >&2
    return 1
  fi
  if ! command -v python3 >/dev/null 2>&1; then
    echo "python3 required to import Kaikki JSONL" >&2
    return 1
  fi
  if [[ ! -f "${IMPORT_SCRIPT}" ]]; then
    echo "Import script missing: ${IMPORT_SCRIPT}" >&2
    return 1
  fi

  python3 "${IMPORT_SCRIPT}" "${source}" "${DB_PATH}" "${MAX_IMPORT_ROWS}"
}

download_and_import_kaikki() {
  if ! command -v curl >/dev/null 2>&1; then
    echo "curl required to download Kaikki dictionary data" >&2
    return 1
  fi
  if [[ ! -f "${IMPORT_SCRIPT}" ]]; then
    echo "Import script missing: ${IMPORT_SCRIPT}" >&2
    return 1
  fi

  echo "Downloading Kaikki English dictionary (streaming, up to ${MAX_IMPORT_ROWS} senses)..."
  # Do not fail the pipeline solely because curl gets SIGPIPE once import hits max rows.
  set +o pipefail
  curl -fsSL --retry 3 --retry-delay 5 "${KAIKKI_URL}" \
    | python3 "${IMPORT_SCRIPT}" "-" "${DB_PATH}" "${MAX_IMPORT_ROWS}"
  local rc=$?
  set -o pipefail
  if [[ "${rc}" -ne 0 ]]; then
    return "${rc}"
  fi
  local count
  count="$(existing_entry_count)"
  if [[ "${count}" -lt "${MIN_ENTRIES}" ]]; then
    echo "Kaikki import produced only ${count} rows (need ${MIN_ENTRIES})" >&2
    return 1
  fi
  return 0
}

download_with_retries() {
  local attempt=1
  while (( attempt <= DOWNLOAD_RETRIES )); do
    echo "Kaikki download attempt ${attempt}/${DOWNLOAD_RETRIES}..."
    if download_and_import_kaikki; then
      return 0
    fi
    echo "Kaikki download/import attempt ${attempt} failed" >&2
    attempt=$((attempt + 1))
    sleep $((attempt * 5))
  done
  return 1
}

existing="$(existing_entry_count)"
if [[ "${FORCE:-0}" != "1" && "${existing}" -ge "${MIN_ENTRIES}" ]]; then
  echo "Dictionary already populated (${existing} rows); skipping install."
  exit 0
fi

if [[ -n "${KAIKKI_JSONL}" ]]; then
  import_kaikki_jsonl "${KAIKKI_JSONL}"
elif [[ "${SKIP_DOWNLOAD:-0}" == "1" ]]; then
  if install_packaged_db; then
    :
  else
    echo "SKIP_DOWNLOAD=1; installing seed dictionary at ${DB_PATH}"
    build_seed_db
  fi
else
  # Fast path: image-shipped DB (same Kaikki-derived content, no multi-GB stream).
  if [[ "${FORCE:-0}" != "1" ]] && install_packaged_db; then
    :
  elif wait_for_network && download_with_retries; then
    :
  elif install_packaged_db; then
    echo "Kaikki download failed; used packaged dictionary fallback." >&2
  else
    echo "Kaikki download/import failed; installing seed dictionary at ${DB_PATH}" >&2
    echo "Will retry on next braillatron-dictionary-data.service run (seed < ${MIN_ENTRIES})." >&2
    build_seed_db
  fi
fi

echo "Dictionary database ready: ${DB_PATH} ($(existing_entry_count) rows)"
