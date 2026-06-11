# Connectivity Follow-Up Checklist

**Project:** Mobile Smart Braille Notetaker & Embosser (Graham Brailler)

**Scope:** Work remaining after Connectd v1 (YouTube audio + Signal messaging)

**Baseline PR:** [PR #1 — braillatron-connectd](https://github.com/grahamthetvi/Graham-Braillatron/pull/1)

**Canonical architecture:** [Master Software Architecture V9](Master%20Software%20Architecture%20V9.md)

---

## How to use this document

This is a **visitation list** — work through sections in order during bring-up and hardening. Check boxes as items are completed on real hardware or in code.

| Where to track work | Best for |
|---------------------|----------|
| **This spec (`specs/`)** | Durable project checklist; survives across conversations and agents |
| **Appendix A in V9** | High-level Implemented / Scaffold / Not addressed status |
| **Cursor Plan + todos** | Active agent session with trackable in-IDE tasks (ephemeral) |
| **GitHub PR / issues** | Assignments, review, and release notes |

For a new Cursor conversation, paste the **Suggested agent prompt** at the bottom of this file.

---

## Phase 0 — Merge and deploy

- [ ] Merge PR #1 (or rebase onto current `main`)
- [ ] Run `deploy/bootstrap-dietpi.sh` or `deploy/install.sh` on Orange Pi 3B
- [ ] Confirm packages installed: `yt-dlp`, `mpv`, `ffmpeg`
- [ ] Confirm `signal-cli` installed via `deploy/install-signal-cli.sh` (aarch64 native)
- [ ] Confirm config installed: `/etc/braillatron/connect.conf`, `youtube.conf`, `messages.conf`
- [ ] Confirm credential dirs exist: `/data/braillatron/credentials/incoming/`, `signal-cli/` (mode `0700`)
- [ ] `systemctl enable braillatron.target && systemctl start braillatron.target`
- [ ] Verify service order: `braillatron-connectd` → `braillatron-ui`

---

## Phase 1 — On-device smoke test (visit every item)

### connectd health

- [ ] `systemctl status braillatron-connectd` — active, no crash loop
- [ ] Settings → Accounts → **Connectivity status** announces "Connectd online"
- [ ] Socket exists: `/run/braillatron/connect.sock`
- [ ] Event file writable: `/run/braillatron/connect.events`

### YouTube audio

- [ ] Launch **YouTube** app from menu
- [ ] Search a public query (no cookies) — results announced via TTS
- [ ] Play a result — audio heard on I2S speaker (PipeWire/pulse → MAX98357A)
- [ ] Shift/TTS pauses or ducks during playback
- [ ] Backspace stops playback and returns to results
- [ ] Export browser cookies → copy to `/data/braillatron/credentials/incoming/cookies.txt`
- [ ] Settings → **Import YouTube cookies** — cookies moved to `youtube-cookies.txt`
- [ ] Play age-gated or subscription content (requires cookies)

### Signal messaging

- [ ] Settings → Accounts → **Link Signal** — URI or instructions announced
- [ ] Approve link on phone (Signal → Linked Devices)
- [ ] Settings → **Signal status** announces "Signal linked"
- [ ] Open **Messages** app — chat list loads
- [ ] Open a thread — recent messages read via TTS
- [ ] Compose and send a reply — received on phone
- [ ] Send message from phone — device announces + haptic (while in Messages app)

---

## Phase 2 — Known bugs to fix (code)

Priority fixes identified during v1 implementation; not yet validated on device.

### P0 — UI freeze / missed messages

- [ ] **Non-blocking Signal link:** `signal.finish_link` currently blocks up to 120s on the connectd socket; UI freezes during Settings → Link Signal. Split into `start_link` + async `link_status` poll or event-driven completion.
- [ ] **Global message events:** `ConnectClient::poll_events()` only runs inside YouTube/Messages apps. Move event drain to `UiApp::poll()` so inbound Signal messages announce from any foreground app.

### P1 — Reliability

- [ ] **Signal account detection:** `linked_account()` directory scan may not match signal-cli layout under `XDG_DATA_HOME`. Validate on device; use `signal-cli listAccounts` if needed.
- [ ] **mpv startup race:** Retry or wait for `/run/braillatron/mpv.sock` before accepting `youtube.play`.
- [ ] **YouTube search shell escaping:** Escape special characters in queries passed to `yt-dlp` shell command (`youtube_backend.cpp`).
- [ ] **Shift/TTS pause toggle:** Track mpv pause state; resume on key release when appropriate.

### P2 — Build / test

- [ ] **ui-test link:** Add connect objects (`connect_client`, `json_utils`, `connect_config`, `connect_defaults`) to `UI_TEST_OBJS` in `daemon-dietpi/Makefile`.
- [ ] **signal-cli version pin:** Confirm `SIGNAL_CLI_VERSION` in `deploy/install-signal-cli.sh` matches a published aarch64 native release.

---

## Phase 3 — Production hardening

- [ ] YouTube cookie missing/expired — TTS warning on app enter and periodic reminder
- [ ] Quick Status inline — include connectd reachability alongside Wi-Fi/battery
- [ ] yt-dlp rate limiting — `--sleep-requests` / backoff on HTTP 429
- [ ] connectd reconnect — UI graceful message when sidecar dies mid-session
- [ ] Credential dir permissions — enforce `0700` at connectd startup
- [ ] Update **Appendix A** in V9 when items move from Scaffold → Implemented

---

## Phase 4 — Feature follow-ups (v1.1+)

### Connectivity

- [ ] **LocalSend → credentials:** Wire `localsend.conf` scaffold to receive files into `credentials/incoming/`
- [ ] **Async connect IPC:** Request IDs + event responses so UI never blocks on connectd
- [ ] **YouTube captions → Braille:** `yt-dlp --write-auto-subs` → text via OutputHub/embosser

### Library (from earlier feasibility work)

- [ ] Public domain books — Gutendex search + Gutenberg download + BRF import (`library.conf`, `LibraryApp`)
- [ ] BARD Public API metadata search (`api.nlsbard.loc.gov`)
- [ ] BARD patron-authenticated downloads (requires user account)

### Deferred integrations (not started)

- [ ] Gmail / Delta Chat (email-shaped messaging)
- [ ] WhatsApp / iMessage — not feasible on this platform; do not pursue unless product direction changes
- [ ] YouTube Data API v3 OAuth — wrong API for playback; cookies remain the path

---

## Reference — v1 file map

| Component | Path |
|-----------|------|
| connectd daemon | `daemon-dietpi/src/connect/` |
| connectd entry | `connect_main.cpp` |
| UI client | `connect_client.cpp` |
| YouTube app | `daemon-dietpi/src/ui/apps/youtube_app.cpp` |
| Messages app | `daemon-dietpi/src/ui/apps/messages_app.cpp` |
| Accounts menu | `output_hub.cpp` → `build_accounts_menu()` |
| Config | `deploy/config/connect.conf`, `youtube.conf`, `messages.conf` |
| systemd | `deploy/systemd/braillatron-connectd.service` |
| signal-cli install | `deploy/install-signal-cli.sh` |

---

## Reference — manual YouTube sign-in (v1)

1. Open a **private/incognito** browser window on a PC or phone.
2. Log into YouTube.
3. Navigate to `https://www.youtube.com/robots.txt` (reduces cookie rotation).
4. Export cookies (Netscape format) with a browser extension.
5. Copy file to device: `/data/braillatron/credentials/incoming/cookies.txt`
6. connectd moves it to `youtube-cookies.txt` (or use Settings → Import YouTube cookies).
7. Re-export approximately every two weeks when playback fails.

---

## Reference — manual Signal link (v1)

1. Settings → Accounts → **Link Signal**
2. On phone: Signal → Settings → Linked Devices → Link New Device
3. Approve the link (QR or link URI if announced)
4. Settings → **Signal status** — confirm "Signal linked"
5. Open Messages app

---

## Suggested agent prompt (copy into new Cursor conversation)

```
Connectd v1 follow-up on Graham Brailler (Orange Pi 3B).

Use specs/Connectivity Follow-Up Checklist.md as the visitation list.
Start with Phase 0–1 on-device validation, then Phase 2 P0 bugs:
- non-blocking Signal link
- global message event polling in UiApp

PR baseline: https://github.com/grahamthetvi/Graham-Braillatron/pull/1
Do not edit the Cursor plan file; update the spec checklist as items complete.
```

---

## Revision log

| Date | Change |
|------|--------|
| 2026-06-11 | Initial checklist after Connectd v1 implementation |
