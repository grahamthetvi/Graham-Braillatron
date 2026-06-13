#!/usr/bin/env bash
# Prepare Gmail OAuth device-flow credentials on Braillatron.
#
# Google Cloud Console steps (one-time, on a PC):
# 1. Create a project at https://console.cloud.google.com/
# 2. APIs & Services → Enable APIs → enable "Gmail API"
# 3. APIs & Services → OAuth consent screen → External → add test user (your Gmail)
# 4. APIs & Services → Credentials → Create credentials → OAuth client ID
#    Application type: TVs and Limited Input devices
# 5. Copy the client ID (ends with .apps.googleusercontent.com)
#
# On the device (as root):
#   sudo braillatron-install-gmail-oauth
#   sudo sh -c 'echo YOUR_CLIENT_ID.apps.googleusercontent.com > /data/braillatron/credentials/gmail/client_id'
#   chmod 600 /data/braillatron/credentials/gmail/client_id
#
# Link account from UI: Settings → Accounts → Link Gmail
# Visit https://www.google.com/device and enter the announced user code.

set -euo pipefail

CRED_DIR="/data/braillatron/credentials/gmail"

install -d -m 700 "${CRED_DIR}"

if [[ ! -f "${CRED_DIR}/client_id" ]]; then
  cat >"${CRED_DIR}/client_id.placeholder" <<'EOF'
# Paste your OAuth client ID on the next line, then:
#   mv client_id.placeholder client_id && chmod 600 client_id
EOF
  chmod 600 "${CRED_DIR}/client_id.placeholder"
  echo "Created ${CRED_DIR} (mode 0700)."
  echo "Add your Google OAuth client ID to ${CRED_DIR}/client_id before linking Gmail."
else
  chmod 700 "${CRED_DIR}"
  chmod 600 "${CRED_DIR}/client_id"
  echo "Gmail credentials directory ready: ${CRED_DIR}"
fi
