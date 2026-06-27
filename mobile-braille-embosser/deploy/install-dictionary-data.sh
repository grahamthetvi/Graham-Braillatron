#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DB_PATH="${DB_PATH:-/data/braillatron/dictionary/en.sqlite}"
KAIKKI_JSONL="${KAIKKI_JSONL:-}"

if [[ "$(id -u)" -ne 0 ]]; then
  echo "install-dictionary-data.sh must run as root (sudo)." >&2
  exit 1
fi

install -d "$(dirname "${DB_PATH}")"

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
  if [[ ! -f "${source}" ]]; then
    echo "Kaikki JSONL not found: ${source}" >&2
    return 1
  fi
  if ! command -v python3 >/dev/null 2>&1; then
    echo "python3 required to import Kaikki JSONL" >&2
    return 1
  fi

  python3 - "${source}" "${DB_PATH}" <<'PY'
import json
import sqlite3
import sys

source, db_path = sys.argv[1], sys.argv[2]
conn = sqlite3.connect(db_path)
conn.execute("PRAGMA journal_mode=WAL")
conn.executescript(
    """
    CREATE TABLE IF NOT EXISTS entries (
      word TEXT NOT NULL,
      pos TEXT,
      definition TEXT NOT NULL
    );
    CREATE INDEX IF NOT EXISTS idx_entries_word ON entries(word COLLATE NOCASE);
    DELETE FROM entries;
    """
)
inserted = 0
with open(source, "r", encoding="utf-8") as handle:
    for line in handle:
        line = line.strip()
        if not line:
            continue
        row = json.loads(line)
        word = row.get("word") or row.get("title")
        if not word:
            continue
        entry_pos = row.get("pos") or ""
        for sense in row.get("senses", []):
            glosses = sense.get("glosses") or []
            if not glosses:
                continue
            pos_text = entry_pos
            if not pos_text:
                tags = sense.get("tags")
                pos_text = tags[0] if isinstance(tags, list) and tags else ""
            definition = "; ".join(str(g) for g in glosses[:3])
            conn.execute(
                "INSERT INTO entries(word,pos,definition) VALUES (?,?,?)",
                (word, str(pos_text), definition),
            )
            inserted += 1
            if inserted >= 500000:
                break
        if inserted >= 500000:
            break
conn.commit()
conn.close()
print(f"Imported {inserted} dictionary rows into {db_path}")
PY
}

if [[ -n "${KAIKKI_JSONL}" ]]; then
  import_kaikki_jsonl "${KAIKKI_JSONL}"
else
  echo "No KAIKKI_JSONL set; installing seed dictionary at ${DB_PATH}"
  build_seed_db
fi

echo "Dictionary database ready: ${DB_PATH}"
