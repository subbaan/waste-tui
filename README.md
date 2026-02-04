# WASTE TUI

A modern terminal user interface for WASTE, the encrypted peer-to-peer file sharing and chat application originally created by Nullsoft in 2003.

![Network View](images/network.png)

## About WASTE

WASTE was a decentralized, encrypted P2P network created by Justin Frankel (of Winamp fame) at Nullsoft. It was briefly released in 2003 before being pulled, but the source code remained available under GPL v2.

This project revives WASTE with a modern Linux TUI client built using [FTXUI](https://github.com/ArthurSonzogni/FTXUI), while maintaining compatibility with the original WASTE protocol.

## Screenshots

| Network | Chat |
|---------|------|
| ![Network](images/network.png) | ![Chat](images/chat.png) |

| Key Manager | Settings |
|-------------|----------|
| ![Keys](images/keymanager.png) | ![Settings](images/settings.png) |

## Current Status

**Version**: 1.8.1

### Working
- Peer connections via encrypted WASTE protocol
- Chat messaging (send/receive in channels)
- Basic file transfers (downloads and uploads with progress)
- Key management (generate, import, export keys)
- Local and remote file search
- File database scanning and sharing

### Not Working / Limitations
- Direct messages (only channel chat works)
- Multiple simultaneous transfers may have issues
- Ports > 32767 may fail (protocol uses `short`)
- Real peer file counts not yet implemented

## Build

Requires CMake 3.14+ and a C++17 compiler.

```bash
cd tui/build
cmake ..
make -j$(nproc)
./waste-tui
```

FTXUI is fetched automatically during the build.

## How WASTE Works

Unlike traditional P2P networks, WASTE creates **private encrypted mesh networks**. There's no central server or public peer discovery - you only connect to people you explicitly trust.

This works through public-key cryptography:
- Each user has a unique RSA key pair (public + private key)
- You can only connect to someone if you have their public key AND they have yours
- All traffic is encrypted end-to-end

Think of it like a private VPN between friends, but for file sharing and chat.

## Quick Start: Connecting Two Peers

Here's how to connect two WASTE clients (e.g., you and a friend):

### Step 1: First Run (Both Users)

Each user runs `waste-tui` for the first time:
```bash
./waste-tui
```
On first run, you'll be prompted to generate encryption keys. Select "Generate New Keys" and wait for RSA-2048 key generation to complete.

### Step 2: Export Your Public Key (Both Users)

1. Press **F5** to open the Keys view
2. Press **e** to export your public key
3. This creates a `.wastekey` file (e.g., `yourname.wastekey`)

### Step 3: Exchange Keys (Out-of-Band)

Send your `.wastekey` file to your friend through any secure method:
- Email, Signal, USB drive, etc.

**Important:** The `.wastekey` file is your PUBLIC key - it's safe to share. Never share your `.pr4` file (that's your private key).

### Step 4: Import Friend's Key (Both Users)

1. Press **F5** to open the Keys view
2. Press **i** to import
3. Enter the path to your friend's `.wastekey` file
4. The key appears in the "Pending" tab
5. Press **t** to trust the key (moves it to "Trusted" tab)

### Step 5: Connect

1. Press **F1** to open the Network view
2. Press **a** to add a peer
3. Enter your friend's IP address and port (default: 4001)
4. The connection should establish (both users need to have completed steps 1-4)

Once connected, you can chat (F4), search files (F2), and transfer files (F3).

### Key Files Reference

| File | Purpose | Share? |
|------|---------|--------|
| `~/.waste.pr4` | Your private key | **NEVER** - keep secret |
| `~/.waste.pr3` | Trusted public keys | No - local keyring |
| `~/.waste.pr0` | Configuration | No |
| `*.wastekey` | Exported public key | Yes - give to peers |

## Configuration

Default port: **4001**

The TUI creates a configuration directory at `~/.waste/` on first run.

To add peers, use the Network view (F1) and press **a** to add a peer address.

## Keybindings

**Global:**
- `F1`-`F6` - Switch views (Network, Search, Transfers, Chat, Keys, Settings)
- `F10` / `Ctrl+D` - Quit
- `Esc` - Cancel/back

**Navigation:**
- `↑`/`↓` or `j`/`k` - Navigate lists
- `Enter` - Select/confirm
- `a` - Add item
- `d` - Delete item
- `?` - Help

**Keys view:**
- `Tab` - Switch between trusted/pending tabs
- `t` - Trust selected key
- `e` - Export your public key
- `i` - Import key file

## Architecture

```
waste/
├── Core networking: connection.cpp, netkern.cpp, mqueue.cpp, listen.cpp, asyncdns.cpp
├── Protocols: m_chat.cpp, m_search.cpp, m_file.cpp, m_ping.cpp, m_keydist.cpp
├── Files: filedb.cpp, xfers.cpp
├── Crypto: rsa/, blowfish.c, sha.cpp, md5c.c
└── TUI client: tui/ (views/, components/, core/)
```

See [DEVELOPMENT.md](DEVELOPMENT.md) for detailed technical documentation.

## Security Notice

The cryptographic primitives in WASTE are from 2003:
- RSA-2048 (still acceptable)
- Blowfish-CBC (outdated, should be AES-GCM)
- MD5 for message integrity (broken, should be SHA-256)
- SHA-1 for key fingerprints (deprecated)

This makes WASTE suitable for **private trusted networks** but not for high-security applications. A security modernization effort using libsodium is planned for a future version.

## License

GPL v2 - See [LICENSE](LICENSE) for details.

Original WASTE code: Copyright (C) 2003 Nullsoft, Inc.
TUI client and Linux fixes: Copyright (C) 2024-2026 Contributors
