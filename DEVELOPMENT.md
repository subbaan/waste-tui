# WASTE Codebase Documentation

**Version**: 1.8.1
**Last Updated**: 2026-02-04

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
| **TUI Client** | **v1.8.1** | `tui/` (Linux) |

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
2. **Browse peers** - not yet implemented (TODO in browsePeer function)
3. **Chat messages** - connections work, message send/receive needs verification

## v1.8.x Fixes

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

### Complete (v1.8.1)
- Key generation/loading (RSA-2048)
- Peer connections via WASTE protocol
- File database scanning and local+remote search
- File transfers (download/upload with progress)
- Chat messaging (send/receive)
- All views: Network (F1), Search (F2), Transfers (F3), Chat (F4), Keys (F5), Settings (F6)
- Key management - export/import `.wastekey` format
- File transfers - XferRecv (downloads) and XferSend (uploads) integrated

### Remaining
- Real peer file counts from protocol
- Direct downloads from browse view

## TUI Keybindings

**Global:** F1-F6 switch views (Network, Search, Transfers, Chat, Keys, Settings), F10/Ctrl+D quit, Esc cancel/back

**Per-view:** ↑↓/jk navigate, Enter select, a=add, d=delete, ?=help

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
- [ ] Security modernization (libsodium)
- [ ] Cross-platform testing
- [ ] Package distribution (AppImage, AUR)
