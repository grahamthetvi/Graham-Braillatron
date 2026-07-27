#!/usr/bin/env bash
# Prepare IMAP + app-password credentials directory for school email on Braillatron.
#
# On a PC, create imap.ini:
#   email=student@school.edu
#   password=your-app-password
#   # optional if auto-detect is wrong:
#   imap_host=outlook.office365.com
#
# Transfer to the device (pick one):
#   1. LocalSend file named imap.ini (or gmail-imap.ini / school-email.ini)
#   2. sudo install -m 600 imap.ini /data/braillatron/credentials/gmail/imap.ini
#   3. Drop into /data/braillatron/credentials/incoming/imap.ini
#
# Then on the device: Settings → Accounts → Link IMAP email
# Status: Settings → Accounts → IMAP status
# Inbox: open the Gmail app (uses IMAP when linked).

set -euo pipefail

CRED_DIR="/data/braillatron/credentials/gmail"
INCOMING="/data/braillatron/credentials/incoming"

install -d -m 700 "${CRED_DIR}"
install -d -m 700 "${INCOMING}"

PLACEHOLDER="${CRED_DIR}/imap.ini.example"
if [[ ! -f "${CRED_DIR}/imap.ini" && ! -f "${PLACEHOLDER}" ]]; then
  cat >"${PLACEHOLDER}" <<'EOF'
# Rename to imap.ini (chmod 600) or send via LocalSend as imap.ini
email=student@school.edu
password=replace-with-app-password
# imap_host=outlook.office365.com
EOF
  chmod 600 "${PLACEHOLDER}"
  echo "Created ${PLACEHOLDER}"
  echo "Add email= and password= (app password), install as ${CRED_DIR}/imap.ini"
else
  chmod 700 "${CRED_DIR}"
  if [[ -f "${CRED_DIR}/imap.ini" ]]; then
    chmod 600 "${CRED_DIR}/imap.ini"
    echo "IMAP credentials present: ${CRED_DIR}/imap.ini"
  else
    echo "IMAP credentials directory ready: ${CRED_DIR}"
  fi
fi
