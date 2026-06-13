#!/usr/bin/env bash
set -euo pipefail

BUNDLED_DIR="${BUNDLED_DIR:-/usr/share/braillatron/spelling}"
CUSTOM_DIR="${CUSTOM_DIR:-/data/braillatron/spelling-lists}"

if [[ "$(id -u)" -ne 0 ]]; then
  echo "install-spelling-data.sh must run as root (sudo)." >&2
  exit 1
fi

install -d "${BUNDLED_DIR}"
install -d "${CUSTOM_DIR}"

cat >"${BUNDLED_DIR}/grade3_week1.json" <<'JSON'
{
  "id": "grade3_week1",
  "name": "Grade 3 Week 1",
  "words": ["about", "after", "again", "animal", "answer", "because", "before", "better", "bring", "carry"]
}
JSON

cat >"${BUNDLED_DIR}/grade4_week1.json" <<'JSON'
{
  "id": "grade4_week1",
  "name": "Grade 4 Week 1",
  "words": ["accept", "accident", "address", "adventure", "against", "already", "although", "amount", "ancient", "appear"]
}
JSON

cat >"${BUNDLED_DIR}/sample.csv" <<'CSV'
word
orange
purple
yellow
CSV

echo "Installed bundled spelling lists to ${BUNDLED_DIR}"
echo "Import custom lists via LocalSend to ${CUSTOM_DIR} (CSV word column or JSON words array)"
