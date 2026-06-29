#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IMPORT_SCRIPT="${SCRIPT_DIR}/braillatron-import-kaikki-dictionary"
if [[ ! -x "${IMPORT_SCRIPT}" ]]; then
  IMPORT_SCRIPT="${ROOT}/deploy/import-kaikki-dictionary.py"
fi
DB_PATH="${DB_PATH:-/data/braillatron/dictionary/en.sqlite}"
KAIKKI_JSONL="${KAIKKI_JSONL:-}"
KAIKKI_URL="${KAIKKI_URL:-https://kaikki.org/dictionary/English/kaikki.org-dictionary-English.jsonl}"
MIN_ENTRIES="${MIN_ENTRIES:-10000}"
MAX_IMPORT_ROWS="${MAX_IMPORT_ROWS:-500000}"

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

build_seed_db() {
  local existing
  existing="$(existing_entry_count)"
  if [[ "${FORCE:-0}" != "1" && "${existing}" -gt 100 ]]; then
    echo "Dictionary already populated (${existing} rows); skipping seed install."
    return 0
  fi

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

  python3 "${IMPORT_SCRIPT}" "${source}" "${DB_PATH}" "${MAX_IMPORT_ROWS}"
}

download_and_import_kaikki() {
  if ! command -v curl >/dev/null 2>&1; then
    echo "curl required to download Kaikki dictionary data" >&2
    return 1
  fi

  echo "Downloading Kaikki English dictionary (streaming, up to ${MAX_IMPORT_ROWS} senses)..."
  set +o pipefail
  curl -fsSL "${KAIKKI_URL}" | python3 "${IMPORT_SCRIPT}" "-" "${DB_PATH}" "${MAX_IMPORT_ROWS}"
  local rc=$?
  set -o pipefail
  return "${rc}"
}

existing="$(existing_entry_count)"
if [[ "${FORCE:-0}" != "1" && "${existing}" -ge "${MIN_ENTRIES}" ]]; then
  echo "Dictionary already populated (${existing} rows); skipping install."
  exit 0
fi

if [[ -n "${KAIKKI_JSONL}" ]]; then
  import_kaikki_jsonl "${KAIKKI_JSONL}"
elif [[ "${SKIP_DOWNLOAD:-0}" == "1" ]]; then
  echo "SKIP_DOWNLOAD=1; installing seed dictionary at ${DB_PATH}"
  build_seed_db
else
  if download_and_import_kaikki; then
    :
  else
    echo "Kaikki download/import failed; installing seed dictionary at ${DB_PATH}" >&2
    build_seed_db
  fi
fi

echo "Dictionary database ready: ${DB_PATH} ($(existing_entry_count) rows)"
