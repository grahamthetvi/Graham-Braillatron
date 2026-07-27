#!/usr/bin/env python3
"""Stream Kaikki JSONL into the Braillatron dictionary SQLite database."""

import json
import sqlite3
import sys


def main() -> int:
    if len(sys.argv) != 4:
        print("usage: import-kaikki-dictionary.py <jsonl-path-or--> <db-path> <max-rows>", file=sys.stderr)
        return 2

    source, db_path, max_rows_raw = sys.argv[1], sys.argv[2], sys.argv[3]
    max_rows = int(max_rows_raw)

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
    handle = sys.stdin if source == "-" else open(source, "r", encoding="utf-8")
    try:
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
                if inserted % 25000 == 0:
                    conn.commit()
                    print(f"… imported {inserted} senses", flush=True)
                if inserted >= max_rows:
                    break
            if inserted >= max_rows:
                break
    finally:
        if handle is not sys.stdin:
            handle.close()

    conn.commit()
    conn.close()
    print(f"Imported {inserted} dictionary rows into {db_path}", flush=True)
    return 0 if inserted > 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
