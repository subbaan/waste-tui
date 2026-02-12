# WASTE Codebase Documentation

**Version**: 1.9.2
**Last Updated**: 2026-02-06

## Overview

WASTE is a decentralized, encrypted P2P file sharing and chat application (circa 2003).

- **Language**: C++ with C components (~21,000 LOC)
- **Platforms**: Windows (GUI), Linux/POSIX (headless + TUI)
- **Encryption**: RSA-2048, Blowfish, SHA-1, MD5
- **License**: GPL v2

## Versions

| Component | Version | Location |
|-----------|---------|----------|
| GUI Client | v1.0b | `main.cpp` (Windows) |
| Server | v1.0a0 | `srvmain.cpp` (POSIX) |
| **TUI Client** | **v1.9.2** | `tui/` (Linux) |

## Build (TUI)

```bash
cd tui/build && cmake .. && make -j$(nproc) && ./waste-tui
```

## Key Files

- Private key: `~/.waste.pr4`
- Public keyring: `~/.waste.pr3` (trusted peer keys)
- Config: `~/.waste.pr0`
- TUI config: `~/.waste/waste-tui.ini`

## 64-bit Compatibility (Critical)

All these files required `unsigned long` → `uint32_t` fixes:

| File | Component |
|------|-----------|
| `rsa/global.h` | UINT4 typedef |
| `blowfish.c/h` | All Blowfish vars |
| `util.cpp` | readBFdata |
| `connection.cpp` | Blowfish vars |
| `tui/core/waste_core.cpp` | writeBFdata |

Without these: RSA keygen hangs, Blowfish produces garbage.

## Linux Networking Fixes (v1.7.3)

These fixes were required for connections to work on Linux:

| File | Issue | Fix |
|------|-------|-----|
| `sockets.h` | `select()` first param | Added `SELECT_NFDS(maxfd)` macro - Windows ignores, Linux needs `maxfd+1` |
| `connection.cpp:438` | select(0,...) | Changed to `select(SELECT_NFDS(m_socket),...)` |
| `asyncdns.cpp` | `gethostbyname()` deprecated | Replaced with thread-safe `getaddrinfo()` |
| `sockets.h` | `inet_addr()` ambiguous | Added `safe_inet_pton()` helper |
| `tui/core/waste_core.cpp` | Missing `loadPKList()` | Added call to load trusted peer keys |
| `tui/core/waste_core.cpp` | `g_config` NULL | Added `g_config = new C_Config()` initialization |

### Debug Logging

Debug logging can be enabled by setting `g_do_log = 1` in waste_core.cpp. Run with:
```bash
./waste-tui 2>log.txt
```

State values in logs:
- 0=ERROR, 1=RESOLVING, 2=CONNECTING, 3=CONNECTED, 4=IDLE, 5=CLOSING, 6=CLOSED

## WASTE Key Exchange (Important)

WASTE uses a "web of trust" model. Before two peers can connect:
1. Each must have the other's public key in their keyring (`.pr3` file)
2. Keys are exchanged out-of-band (copy files, etc.)

**Quick test workaround:** Copy the same `.pr4` file to both machines - they'll have the same identity and trust themselves.

## Known Limitations

1. **Ports > 32767** may fail (uses `short`)
2. **Multiple simultaneous transfers** may have issues
3. **Real peer file counts** not yet retrieved from protocol

## Version History

### v1.9.2: Settings Wired to Core

- **Accept file requests toggle** - Wired to `g_accept_downloads` bit 1. Toggling in Settings immediately enables/disables responding to peer file requests. Persisted to config.
- **Upload bandwidth throttle** - Wired to `g_throttle_flag` bit 2 + `g_throttle_send`. Toggle enables/disables, Enter edits the KB/s value. Enforced by `mqueuelist.cpp` send throttling.
- **Download bandwidth throttle** - Wired to `g_throttle_flag` bit 1 + `g_throttle_recv`. Same toggle+edit pattern. Enforced by `mqueuelist.cpp` recv throttling.
- **Removed Max Connections** - Was a placeholder with no real core support (`g_keepup` controls auto-connect, not a hard limit). Removed from UI and state.
- **Settings load/save** - `downloadflags`, `throttleflag`, `throttlesend`, `throttlerecv` now persisted in `waste-tui.ini` using same config keys as original WASTE.
- **Startup sync** - Throttle and accept-incoming state loaded from core into UI on startup.

### v1.9.1: Real Peer Nicknames

- **Real nicknames from protocol** - Peer nicknames are now extracted from WASTE protocol messages instead of using random placeholders. Nicknames are learned from three sources: MESSAGE_PING (periodic broadcasts), MESSAGE_CHAT (sender field), and MESSAGE_CHAT_REPLY (delivery confirmations).
- **Fixed MESSAGE_CHAT_REPLY parsing** - Split CHAT and CHAT_REPLY handlers since they have different data formats. CHAT_REPLY uses `C_MessageChatReply` (nick only), not `C_MessageChat`.
- **New `onPeerNicknameChanged` callback** - Dedicated callback for nickname updates, properly propagates from core to UI state.
- **`setListenPort()` implemented** - Deletes and rebinds `g_listen` on the new port. Changes from Settings are now persisted to config.
- **`setNetworkName()` implemented** - Computes SHA-1 hash into `g_networkhash` (used by Blowfish for connection encryption). Empty name clears the hash for open network mode.
- **Network name in Settings** - Added editable Network field to Identity section (Enter to edit, Enter to save).
- **Settings port changes wired** - Editing listen port in Settings now calls the core and saves config (previously only updated UI state).
- **`cancelSearch()` implemented** - Clears `g_last_scanid_used` so incoming SEARCH_REPLY messages are silently dropped. Fires `onSearchComplete` callback.
- **Search reply GUID filtering** - SEARCH_REPLY handler now checks `g_last_scanid` against the message GUID (matching the original Windows code), so stale results from cancelled/previous searches are ignored.
- **Escape to cancel search** - Pressing Escape in search view cancels the active search and clears results.

### v1.9.0: Direct Messages & Clickable URLs

- **Direct messages** - Private peer-to-peer chat using `@nickname` convention over MESSAGE_CHAT broadcast. DMs are filtered client-side so only sender and recipient display them. Press `d` in chat view to start a DM.
- **Clickable URLs** - URLs in chat messages (`https://`, `http://`, `ftp://`, `www.`) are detected and rendered as clickable hyperlinks using FTXUI's OSC 8 `hyperlink()` decorator. Works in modern terminals (kitty, alacritty, wezterm, gnome-terminal, etc.).
- **DM room auto-creation** - Incoming DMs automatically create a room in the DIRECT section of the chat sidebar, named `@peernick`.

### v1.8.x

- **v1.8.1: Browse query format** - Fixed browse to use `/nickname/path/*` format (WASTE requires peer nickname in query)
- **v1.8.0: Peer file browsing** - Browse view with scrollable file listing, Enter to navigate directories
- **Folder browser scrolling** - Fixed modal scrolling with focus on selected item
- **Table scrolling** - Added yframe+focus for auto-scroll to selected row
- **v1.7.9: File transfers** - Downloads/uploads with progress, speed fixes (g_conspeed 4→20000)

## Directory Structure

```
waste/
├── Core: connection.cpp, netkern.cpp, mqueue.cpp, listen.cpp, asyncdns.cpp
├── Protocols: m_chat.cpp, m_search.cpp, m_file.cpp, m_ping.cpp, m_keydist.cpp
├── Files: filedb.cpp, xfers.cpp
├── Crypto (rsa/): rsa.c, blowfish.c, sha.cpp, md5c.c, nn.c, prime.c, r_random.c
├── Win GUI: main.cpp, prefs.cpp, xferwnd.cpp, srchwnd.cpp, d_chat.cpp
├── Server: srvmain.cpp
├── Utils: util.cpp, config.cpp, platform.h
└── TUI: tui/ (views/, components/, core/)
```

## TUI Status

### Complete (v1.9.2)
- Key generation/loading (RSA-2048)
- Peer connections via WASTE protocol
- Real peer nicknames from PING, CHAT, and CHAT_REPLY messages
- File database scanning and local+remote search
- Search cancellation (Escape key, GUID filtering)
- File transfers (download/upload with progress)
- Chat messaging (channel and direct messages)
- Clickable URLs in chat (OSC 8 hyperlinks)
- All views: Network (F1), Search (F2), Transfers (F3), Chat (F4), Keys (F5), Settings (F6)
- Key management - export/import `.wastekey` format
- Settings fully wired: listen port, network name, nickname, shared dirs, bandwidth throttling, accept file requests
- All settings persisted to `waste-tui.ini`

### Remaining
- Real peer file counts from protocol (currently shows 0 for real peers)
- Direct downloads from browse view (browse works, but download button not integrated)
- Interface settings section (placeholder - "coming soon")
- Download path configuration (hardcoded to `g_download_path`)

### Session Notes (for next coding session)

**Where we left off (v1.9.2):** All three `waste_core.cpp` TODOs are resolved. All settings that map to real WASTE globals are wired. Code reviewed, no critical bugs.

**Architecture refresher:**
- `app.cpp` (~3200 lines) - All 7 views inline, event handling, FTXUI components
- `waste_core.cpp` (~2550 lines) - WASTE protocol integration, pimpl pattern, background event loop
- `state.h` - Thread-safe centralized state, mutex-protected
- Data flow: User input → app.cpp → core API → WASTE globals → callback → post() to UI thread → state update → render

**Key patterns:**
- Settings: Enter toggles `settingsEditMode_`, edits go to `settingsEditBuffer_`, Enter again commits to state + core + saveConfig
- Checkboxes: Space toggles state + calls core method + saveConfig
- Field navigation: `selectedSettingsItem_` with `maxItems` per section (Network=4, Sharing=dynamic, Identity=2, Interface=0)
- Callbacks from core run on background thread, must use `post()` lambda + `refresh()` to update UI

**Most impactful next tasks (in suggested order):**
1. **Real peer file counts** - `C_MessagePing` has file count fields. Could extract during PING handling (already parsing pings for nicks). Display in Network view peer table.
2. **Browse → download integration** - Browse view has 'd' key handler but download initiation may not properly construct the `guididx` hash for `downloadFile()`.
3. **Interface settings** - Currently empty placeholder. Could add: color theme toggle, debug log toggle (`g_do_log`).
4. **Download path config** - `g_download_path` is hardcoded. Add to Settings, save/load in config.
5. **View file refactoring** - All views are inline in app.cpp (~3200 lines). Stub view files exist in `views/` ready for extraction.

**Known non-issues (reviewed and OK):**
- `-Wwrite-strings` warnings throughout waste_core.cpp - from legacy WASTE C API taking `char*` not `const char*`. Cosmetic only.
- `filesShared` always 0 for real peers - correct until peer file counts are wired.
- `g_accept_downloads` default 7 (bits 0+1+2) - matches original WASTE. UI toggles bit 0 only, other bits left enabled.
- `g_keepup` (max auto-connect peers) not exposed in TUI - deliberate, it's not a user-facing setting.

## TUI Keybindings

**Global:** F1-F6 switch views (Network, Search, Transfers, Chat, Keys, Settings), F10/Ctrl+D quit, Esc cancel/back

**Per-view:** ↑↓/jk navigate, Enter select, a=add, d=delete, ?=help

**Chat view:** Enter=send, ↑↓/jk=navigate rooms, J=join channel, d=direct message, l=leave room

**Keys view:** Tab=switch trusted/pending, t=trust, d=delete, i=import, e=export

## Security Modernization (Post-TUI)

Current crypto is from 2003 and needs upgrading:

| Component | Current | Target | Priority |
|-----------|---------|--------|----------|
| Message Integrity | MD5 | SHA-256 | **Critical** |
| Session Encryption | Blowfish-CBC | AES-256-GCM | High |
| Key Verification | SHA-1 | SHA-256 | Medium |
| Key Exchange | RSA-2048 | RSA-3072/4096 option | Low |
| RNG | Custom | OS CSPRNG | Low |

**Approach:** Add libsodium, implement upgrades, clean protocol break (no legacy interop).

## Future Work

- [ ] Transfer progress updates to UI (wire onTransferProgress callback)
- [ ] Download path configuration in Settings view
- [ ] Real peer nicknames from MESSAGE_PING protocol
- [ ] Real file counts from peers
- [ ] Direct downloads from browse view
- [ ] Security modernization (libsodium)
- [ ] Cross-platform testing
- [ ] Package distribution (AppImage, AUR)
