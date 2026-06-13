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

- [x] Merge PR #3 (Multi-App Integration v1.2)
- [ ] Run `deploy/bootstrap-dietpi.sh` or `deploy/install.sh` on Orange Pi 3B
- [ ] Run `braillatron-verify-install` after install
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
- [ ] Settings → Accounts → **Connectivity status** announces "Connect daemon online"
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

- [x] **Non-blocking Signal link:** `signal.finish_link` no longer blocks the UI. `signal.start_link` runs async via connectd job queue; `signal.link_status` polls state; `signal.link_completed` / `signal.link_failed` events announce completion.
- [x] **Global message events:** `ConnectClient::poll_events()` runs in `UiApp::poll()`; inbound Signal messages announce from any foreground app.

### P1 — Reliability

- [x] **Signal account detection:** `linked_account()` scans data dir, nested `data/` layout, and falls back to `signal-cli listAccounts`.
- [x] **mpv startup race:** `MpvService::ensure_started()` waits for IPC socket; YouTube play retries load after wait.
- [x] **YouTube search shell escaping:** Search terms passed through shell-safe quoting in `youtube_backend.cpp`.
- [x] **Shift/TTS pause toggle:** Hold Shift pauses via `media.set_pause`; release resumes (tracked in `OutputHub`).

### P2 — Build / test

- [x] **ui-test link:** Add connect objects (`connect_client`, `json_utils`, `connect_config`, `connect_defaults`) to `UI_TEST_OBJS` in `daemon-dietpi/Makefile`.
- [ ] **signal-cli version pin:** Confirm `SIGNAL_CLI_VERSION` in `deploy/install-signal-cli.sh` matches a published aarch64 native release.

---

## Phase 3 — Production hardening

- [ ] YouTube cookie missing/expired — TTS warning on app enter and periodic reminder
- [x] Quick Status inline — include connectd reachability alongside Wi-Fi/battery
- [ ] yt-dlp rate limiting — `--sleep-requests` / backoff on HTTP 429
- [ ] connectd reconnect — UI graceful message when sidecar dies mid-session
- [ ] Credential dir permissions — enforce `0700` at connectd startup
- [ ] Update **Appendix A** in V9 when items move from Scaffold → Implemented

---

## Phase 4 — Feature follow-ups (v1.1+)

### Connectivity

- [ ] **LocalSend → credentials:** Wire `localsend.conf` scaffold to receive files into `credentials/incoming/`
- [x] **Async connect IPC:** Request IDs + `connect.response` events so UI never blocks on connectd long operations (`ConnectJobQueue`, `ConnectClient::request_async`)
- [ ] **YouTube captions → Braille:** `yt-dlp --write-auto-subs` → text via OutputHub/embosser

### Library (from earlier feasibility work)

- [x] Public domain books — Gutendex search + Gutenberg download + EPUB/DAISY reading (`library.conf`, `LibraryApp`, `library_backend.cpp`)
- [ ] BARD Public API integration
- [ ] BARD patron-authenticated downloads
- [ ] Bookshare integration
- [ ] BARD Public API metadata search (`api.nlsbard.loc.gov`)
- [ ] BARD patron-authenticated downloads (requires user account)

### Deferred integrations (not started)

- [x] Gmail — OAuth device flow, inbox/read/compose/reply, archive/star/delete, BRF export (`gmail_backend.cpp`, `gmail_app.cpp`)
- [ ] Delta Chat (email-shaped messaging)
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
| 2026-06-13 | P1 reliability fixes: signal listAccounts fallback, mpv socket wait, yt-dlp shell escape, Shift hold-to-pause |
| 2026-06-13 | UX polish: Messages thread refresh, Quick Status connectd ping, Document Look up stub, verify-install script |
| 2026-06-13 | Phases 2–8: Document STT, Contacts, Music, Weather, Podcasts/Radio, Library EPUB/DAISY/Gutendex, Gmail OAuth |
| 2026-06-13 | Phase 0 connectd hardening: async IPC, non-blocking Signal link, global event polling |
| 2026-06-11 | Initial checklist after Connectd v1 implementation |
