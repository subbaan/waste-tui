/*
    WASTE TUI - Core Integration Layer
    Bridges the WASTE P2P core with the FTXUI-based TUI

    This is a work-in-progress implementation. The full integration
    requires adapting WASTE's global state model to work with the TUI.
*/

#include "waste_core.h"
#include <filesystem>
#include <fstream>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <algorithm>
#include <cctype>

// Include WASTE headers
#include "main.h"
#include "sockets.h"
#include "util.h"
#include "srchwnd.h"
#include "netkern.h"
#include "asyncdns.h"
#include "listen.h"
#include "connection.h"
#include "mqueue.h"
#include "mqueuelist.h"
#include "m_chat.h"
#include "m_ping.h"
#include "m_search.h"
#include "m_file.h"
#include "config.h"
#include "filedb.h"
#include "xfers.h"
#include "sha.h"
#include "itemlist.h"

extern "C" {
#include "rsa/r_random.h"
}

// Define global variables that the WASTE core expects
// These would normally be in main.cpp for the Windows GUI version
C_Listen *g_listen = nullptr;
C_MessageQueueList *g_mql = nullptr;
C_Config *g_config = nullptr;
C_AsyncDNS *g_dns = nullptr;

char g_config_prefix[1024] = "";
char g_config_mainini[1024] = "";

int g_extrainf = 1;
int g_conspeed = 20000;  // T3/LAN (kbps) - see util.cpp conspeed_speeds[]
int g_route_traffic = 1;
int g_do_log = 0;  // Debug logging disabled for release
int g_forceip = 0;
int g_forceip_addr = 0;
int g_use_accesslist = 0;
int g_keydist_flags = 0;
char g_regnick[32] = "";

R_RSA_PRIVATE_KEY g_key;
unsigned char g_pubkeyhash[SHA_OUTSIZE] = {0};

// Network configuration
int g_port = 4001;
unsigned char g_networkhash[SHA_OUTSIZE] = {0};
int g_use_networkhash = 0;

// Throttling
int g_throttle_flag = 0;
int g_throttle_send = 0;
int g_throttle_recv = 0;

// Client IDs and ping tracking
T_GUID g_client_id;
char g_client_id_str[33] = "";
T_GUID g_last_scanid;
int g_last_scanid_used = 0;
T_GUID g_last_pingid;
int g_last_pingid_used = 0;

// Search cache
SearchCacheItem *g_searchcache[SEARCHCACHE_NUMITEMS] = {nullptr};

// File database
C_FileDB *g_database = nullptr;
C_FileDB *g_newdatabase = nullptr;
int g_accept_downloads = 1;  // Allow responding to file requests
char *g_def_extlist = (char*)"mp3;ogg;flac;wav;avi;mkv;mp4;zip;rar;7z;pdf;doc;txt;";

// File transfers (global lists like in main.cpp)
C_ItemList<XferSend> g_sends;
C_ItemList<XferRecv> g_recvs;

// Download path (config-driven)
static std::string g_download_path;

// Forward declaration for message callback
namespace waste {
    class WasteCore;
}
static waste::WasteCore* g_waste_core_instance = nullptr;

// Browse mode state (static for callback access)
static std::string g_browse_path;
static std::mutex g_browse_mutex;

// Message callback - called by g_mql when messages arrive
// Note: main_MsgCallback is already declared extern in main.h
void main_MsgCallback(T_Message *message, C_MessageQueueList *_this, C_Connection *cn) {
    if (!g_waste_core_instance) return;

    // Handle different message types
    switch (message->message_type) {
        case MESSAGE_CHAT_REPLY:
            // Chat reply - contains only the replying peer's nickname
            debug_printf("[CHAT_REPLY] Received (len=%d, data=%p)\n",
                message->message_length, (void*)message->data);

            if (message->data && message->message_length > 0) {
                C_MessageChatReply repl(message->data);
                char *n = repl.getnick();
                if (n && n[0] && n[0] != '.' && strlen(n) < 24) {
                    // Update peer nickname from chat reply
                    if (cn && g_waste_core_instance) {
                        struct in_addr addr;
                        addr.s_addr = cn->get_remote();
                        std::string peerAddr = inet_ntoa(addr);
                        debug_printf("[CHAT_REPLY] Got nick '%s' from peer %s\n",
                                     n, peerAddr.c_str());
                        g_waste_core_instance->updatePeerNickname(peerAddr, n);
                    }
                }
            }
            break;

        case MESSAGE_CHAT:
            // Chat message received - extract and forward to TUI
            debug_printf("[CHAT] Received MESSAGE_CHAT (len=%d, data=%p)\n",
                message->message_length, (void*)message->data);

            if (message->data && message->message_length > 0) {
                C_MessageChat chat(message->data);

                std::string chatStr = chat.get_chatstring();
                std::string src = chat.get_src();
                std::string dest = chat.get_dest();

                debug_printf("[CHAT] Parsed: src='%s' dest='%s' content='%s'\n",
                    src.c_str(), dest.c_str(), chatStr.c_str());

                // Update peer nickname from chat source
                if (!src.empty() && src[0] != '.' && src.length() < 24 &&
                    cn && g_waste_core_instance) {
                    struct in_addr addr;
                    addr.s_addr = cn->get_remote();
                    std::string peerAddr = inet_ntoa(addr);
                    g_waste_core_instance->updatePeerNickname(peerAddr, src);
                }

                // Handle user presence commands
                if (chatStr.length() > 6 && chatStr.substr(0, 6) == "/nick/") {
                    // Nick change: /nick/<oldnick> - user renamed from oldnick to src
                    std::string oldNick = chatStr.substr(6);
                    if (g_waste_core_instance->onUserPresence) {
                        // Report old nick leaving
                        g_waste_core_instance->onUserPresence(dest, oldNick, false);
                        // Report new nick joining
                        g_waste_core_instance->onUserPresence(dest, src, true);
                    }
                    // Also send as system message to chat
                    if (g_waste_core_instance->onChatMessage) {
                        waste::ChatMessage msg;
                        msg.room = dest;
                        msg.sender = "";
                        msg.content = "*** " + oldNick + " is now known as " + src;
                        msg.timestamp = std::chrono::system_clock::now();
                        msg.isSystem = true;
                        g_waste_core_instance->onChatMessage(msg);
                    }
                }
                else if (chatStr == "/join") {
                    // User joined channel
                    if (g_waste_core_instance->onUserPresence) {
                        g_waste_core_instance->onUserPresence(dest, src, true);
                    }
                    // Also send as system message
                    if (g_waste_core_instance->onChatMessage) {
                        waste::ChatMessage msg;
                        msg.room = dest;
                        msg.sender = "";
                        msg.content = "*** " + src + " has joined " + dest;
                        msg.timestamp = std::chrono::system_clock::now();
                        msg.isSystem = true;
                        g_waste_core_instance->onChatMessage(msg);
                    }
                }
                else if (chatStr == "/part" || chatStr == "/leave") {
                    // User left channel
                    if (g_waste_core_instance->onUserPresence) {
                        g_waste_core_instance->onUserPresence(dest, src, false);
                    }
                    // Also send as system message
                    if (g_waste_core_instance->onChatMessage) {
                        waste::ChatMessage msg;
                        msg.room = dest;
                        msg.sender = "";
                        msg.content = "*** " + src + " has left " + dest;
                        msg.timestamp = std::chrono::system_clock::now();
                        msg.isSystem = true;
                        g_waste_core_instance->onChatMessage(msg);
                    }
                }
                else {
                    // Regular chat message - forward to TUI
                    if (g_waste_core_instance->onChatMessage) {
                        waste::ChatMessage msg;
                        msg.sender = src;
                        msg.content = chatStr;
                        msg.timestamp = std::chrono::system_clock::now();
                        msg.isSystem = false;

                        // Check for direct message (dest starts with @)
                        if (dest.length() > 1 && dest[0] == '@') {
                            std::string targetNick = dest.substr(1);
                            std::string myNick = g_regnick[0] ? g_regnick : "";

                            // Only show DM if addressed to us or sent by us
                            if (!myNick.empty() && targetNick != myNick && src != myNick) {
                                debug_printf("[CHAT] DM not for us: target='%s' us='%s'\n",
                                    targetNick.c_str(), myNick.c_str());
                                break;
                            }

                            // Normalize room name: always use the other person's nick
                            if (src == myNick) {
                                msg.room = "@" + targetNick;
                            } else {
                                msg.room = "@" + src;
                            }

                            debug_printf("[CHAT] DM: from='%s' to='%s' room='%s'\n",
                                src.c_str(), targetNick.c_str(), msg.room.c_str());
                        } else {
                            msg.room = dest;
                        }

                        // Check for action messages (/me)
                        if (chatStr.length() > 4 && chatStr.substr(0, 4) == "/me ") {
                            msg.content = "* " + src + " " + chatStr.substr(4);
                            msg.isSystem = true;
                        }

                        debug_printf("[CHAT] Invoking onChatMessage callback: room='%s' sender='%s'\n",
                            msg.room.c_str(), msg.sender.c_str());
                        g_waste_core_instance->onChatMessage(msg);
                    } else {
                        debug_printf("[CHAT] ERROR: onChatMessage callback not registered!\n");
                    }
                }

                // Send reply with our nickname
                if (g_regnick[0]) {
                    C_MessageChatReply rep;
                    rep.setnick(g_regnick);

                    T_Message replyMsg = {0,};
                    replyMsg.message_guid = message->message_guid;
                    replyMsg.data = rep.Make();
                    if (replyMsg.data) {
                        replyMsg.message_type = MESSAGE_CHAT_REPLY;
                        replyMsg.message_length = replyMsg.data->GetLength();
                        _this->send(&replyMsg);
                    }
                }
            }
            break;

        case MESSAGE_PING:
        {
            // Parse ping to extract peer nickname
            C_MessagePing rep(message->data);

            // Extract nickname from ping and update peer
            if (rep.m_nick[0] && rep.m_nick[0] != '#' && rep.m_nick[0] != '&' &&
                rep.m_nick[0] != '.' && strlen(rep.m_nick) < 24)
            {
                // Map to peer address via the connection that delivered this message
                if (cn && g_waste_core_instance) {
                    struct in_addr addr;
                    addr.s_addr = cn->get_remote();
                    std::string peerAddr = inet_ntoa(addr);
                    debug_printf("[PING] Got nick '%s' from peer %s\n",
                                 rep.m_nick, peerAddr.c_str());
                    g_waste_core_instance->updatePeerNickname(peerAddr, rep.m_nick);
                }
            }
        }
        break;

        case MESSAGE_SEARCH:
            // Search request received - respond with local file matches
            if ((g_accept_downloads & 1) && g_database && g_database->GetNumFiles() > 0) {
                C_MessageSearchRequest req(message->data);
                char* searchStr = req.get_searchstring();
                int minSpeed = req.get_min_conspeed();

                if (g_conspeed >= minSpeed && searchStr && searchStr[0]) {
                    C_MessageSearchReply repl;
                    repl.set_conspeed(g_conspeed);
                    repl.set_guid(&g_client_id);
                    g_database->Search(searchStr, &repl, _this, message, main_MsgCallback);
                }
            }
            break;

        case MESSAGE_SEARCH_REPLY:
            // Search results received - parse and forward to TUI
            if (message->data && message->message_length > 0) {
                C_MessageSearchReply reply(message->data);

                // Check if we're in browse mode (browsePath set)
                bool isBrowseMode = false;
                std::string browsePath;
                {
                    std::lock_guard<std::mutex> lock(g_browse_mutex);
                    isBrowseMode = !g_browse_path.empty();
                    browsePath = g_browse_path;
                }

                int numItems = reply.get_numitems();
                debug_printf("[SEARCH_REPLY] Received: numItems=%d, isBrowseMode=%d, browsePath='%s'\n",
                            numItems, isBrowseMode ? 1 : 0, browsePath.c_str());

                if (isBrowseMode && g_waste_core_instance->onBrowseResults) {
                    // Parse as browse results
                    std::vector<waste::BrowseEntry> entries;

                    for (int i = 0; i < numItems; i++) {
                        int id;
                        char filename[SEARCHREPLY_MAX_FILESIZE];
                        char metadata[SEARCHREPLY_MAX_METASIZE];
                        int sizeLow, sizeHigh, fileTime;

                        if (reply.get_item(i, &id, filename, metadata, &sizeLow, &sizeHigh, &fileTime) == 0) {
                            debug_printf("[BROWSE] Item %d: id=%d file='%s' meta='%s' size=%d/%d\n",
                                        i, id, filename, metadata, sizeLow, sizeHigh);
                            waste::BrowseEntry entry;

                            // WASTE returns entry names for the current directory level
                            // (e.g., "Music", "song.mp3" — not full paths)
                            std::string entryName = filename;

                            // Strip trailing slash if present
                            if (!entryName.empty() && entryName.back() == '/') {
                                entryName = entryName.substr(0, entryName.length() - 1);
                            }

                            entry.name = entryName;
                            entry.fullPath = entryName;

                            // WASTE marks directories with metadata "Directory" and id=-1
                            entry.isDirectory = (id == -1) ||
                                               (strcmp(metadata, "Directory") == 0);

                            entry.size = ((uint64_t)sizeHigh << 32) | (uint64_t)(unsigned int)sizeLow;
                            entry.fileId = id;  // v_index for downloads (-1 for dirs)

                            if (!entry.name.empty()) {
                                entries.push_back(entry);
                            }
                        }
                    }

                    // Get peer info
                    char guidStr[33];
                    MakeID128Str(reply.get_guid(), guidStr);

                    debug_printf("[BROWSE] Calling onBrowseResults: peer=%s, entries=%zu\n",
                                guidStr, entries.size());
                    g_waste_core_instance->onBrowseResults(guidStr, entries);

                    // Clear browse mode so subsequent SEARCH_REPLYs go to search handler
                    {
                        std::lock_guard<std::mutex> lock(g_browse_mutex);
                        g_browse_path.clear();
                    }
                } else if (g_last_scanid_used &&
                           !memcmp(&g_last_scanid, &message->message_guid, sizeof(T_GUID))) {
                    // Regular search results - only accept if GUID matches our active search
                    for (int i = 0; i < numItems; i++) {
                        int id;
                        char filename[SEARCHREPLY_MAX_FILESIZE];
                        char metadata[SEARCHREPLY_MAX_METASIZE];
                        int sizeLow, sizeHigh, fileTime;

                        if (reply.get_item(i, &id, filename, metadata, &sizeLow, &sizeHigh, &fileTime) == 0) {
                            if (g_waste_core_instance->onSearchResult) {
                                waste::SearchResult result;
                                result.filename = filename;
                                result.size = ((uint64_t)sizeHigh << 32) | (uint64_t)(unsigned int)sizeLow;
                                result.type = metadata;
                                result.sources = 1;

                                // Get peer GUID string
                                char guidStr[33];
                                MakeID128Str(reply.get_guid(), guidStr);
                                result.user = guidStr;
                                result.hash = std::string(guidStr) + ":" + std::to_string(id);

                                g_waste_core_instance->onSearchResult(result);
                            }
                        }
                    }

                    if (g_waste_core_instance->onSearchComplete) {
                        g_waste_core_instance->onSearchComplete();
                    }
                }
            }
            break;

        case MESSAGE_FILE_REQUEST:
            // Handle incoming file request (someone wants to download from us)
            {
                C_FileSendRequest *r = new C_FileSendRequest(message->data);

                // Check if this request is for us
                if (!memcmp(r->get_guid(), &g_client_id, sizeof(g_client_id))) {
                    int n = g_sends.GetSize();
                    int x;

                    // Check if this is a follow-up to an existing transfer
                    for (x = 0; x < n; x++) {
                        if (!memcmp(r->get_prev_guid(), g_sends.Get(x)->get_guid(), 16)) {
                            // Update existing transfer
                            g_sends.Get(x)->set_guid(&message->message_guid);
                            g_sends.Get(x)->onGotMsg(r);
                            break;
                        }
                    }

                    // New file request
                    if (x == n && !r->is_abort()) {
                        int maxUploads = g_config ? g_config->ReadInt((char*)"ul_limit", 160) : 160;
                        if (n < maxUploads) {
                            char fn[2048];
                            fn[0] = 0;
                            int idx = r->get_idx();

                            if (idx >= 0 && g_database) {
                                g_database->GetFile(idx, fn, NULL, NULL, NULL);
                            }

                            if (fn[0]) {
                                XferSend *xfer = new XferSend(_this, &message->message_guid, r, fn);
                                char *err = xfer->GetError();

                                if (err) {
                                    debug_printf("[XFER] Upload failed: %s\n", err);
                                    delete xfer;
                                } else {
                                    g_sends.Add(xfer);
                                    debug_printf("[XFER] Started upload: %s\n", fn);

                                    // Notify TUI of new upload
                                    if (g_waste_core_instance->onTransferAdded) {
                                        waste::TransferInfo info;
                                        info.id = (int)(intptr_t)xfer;  // Use pointer as ID
                                        info.filename = xfer->GetName();
                                        info.direction = waste::TransferDirection::Upload;
                                        info.status = waste::TransferStatus::Active;
                                        unsigned int sizeLow, sizeHigh;
                                        xfer->GetSize(&sizeLow, &sizeHigh);
                                        info.totalSize = ((uint64_t)sizeHigh << 32) | sizeLow;
                                        info.transferred = 0;
                                        info.speedKBps = 0;
                                        info.peer = r->get_nick() ? r->get_nick() : "";
                                        g_waste_core_instance->onTransferAdded(info);
                                    }
                                }
                            } else {
                                // File not found - send error reply
                                T_Message msg = {0,};
                                C_FileSendReply reply;
                                reply.set_error(1);
                                msg.data = reply.Make();
                                if (msg.data) {
                                    msg.message_type = MESSAGE_FILE_REQUEST_REPLY;
                                    msg.message_length = msg.data->GetLength();
                                    msg.message_guid = message->message_guid;
                                    g_mql->send(&msg);
                                }
                            }
                        }
                    }
                }
                delete r;
            }
            break;

        case MESSAGE_FILE_REQUEST_REPLY:
            // Handle file data/header from peer (we're downloading)
            {
                int n = g_recvs.GetSize();
                for (int x = 0; x < n; x++) {
                    if (!memcmp(g_recvs.Get(x)->get_guid(), &message->message_guid, 16)) {
                        C_FileSendReply *reply = new C_FileSendReply(message->data);
                        g_recvs.Get(x)->onGotMsg(reply);
                        // Note: onGotMsg takes ownership of reply, don't delete
                        break;
                    }
                }
            }
            break;

        default:
            break;
    }
}

// Helper: Write private key to file (adapted from keygen.cpp)
// Note: Uses uint32_t for 64-bit compatibility (unsigned long is 8 bytes on 64-bit Linux)
static void writeBFdata(FILE *out, BLOWFISH_CTX *ctx, void *data, unsigned int len,
                        uint32_t *cbcl, uint32_t *cbcr, int *lc) {
    unsigned int x;
    uint32_t *p = (uint32_t *)data;
    for (x = 0; x < len; x += 8) {
        uint32_t pp[2];
        pp[0] = p[0];
        pp[1] = p[1];
        pp[0] ^= *cbcl;
        pp[1] ^= *cbcr;
        Blowfish_Encrypt(ctx, pp, pp+1);
        *cbcl = pp[0];
        *cbcr = pp[1];
        for (int c = 0; c < 8; c++) {
            fprintf(out, "%02X", ((unsigned char *)pp)[c]);
            if (++*lc % 30 == 0) fprintf(out, "\n");
        }
        p += 2;
    }
}

static int writePrivateKey(const char *fn, R_RSA_PRIVATE_KEY *key, R_RANDOM_STRUCT *rnd,
                           unsigned char *passhash) {
    FILE *fp = fopen(fn, "wt");
    if (!fp) return 1;

    int lc = 8;
    fprintf(fp, "WASTE_PRIVATE_KEY 10 %d\n", key->bits);

    uint32_t tl[2];
    R_GenerateBytes((unsigned char *)&tl, 8, rnd);
    for (int x = 0; x < 8; x++) {
        fprintf(fp, "%02X", (tl[x/4] >> ((x&3)*8)) & 0xff);
    }

    BLOWFISH_CTX c;
    Blowfish_Init(&c, passhash, SHA_OUTSIZE);
    char buf[9] = "PASSWORD";
    writeBFdata(fp, &c, buf, 8, tl, tl+1, &lc);

    #define WPK(x) writeBFdata(fp, &c, key->x, sizeof(key->x), tl, tl+1, &lc);
    WPK(modulus)
    WPK(publicExponent)
    WPK(exponent)
    WPK(prime)
    WPK(primeExponent)
    WPK(coefficient)
    #undef WPK

    if (lc % 30) fprintf(fp, "\n");
    fprintf(fp, "WASTE_PRIVATE_KEY_END\n");
    fclose(fp);

    memset(&c, 0, sizeof(c));
    return 0;
}
// #include "../../platform.h"
// #include "../../connection.h"
// #include "../../listen.h"
// #include "../../asyncdns.h"
// #include "../../mqueue.h"
// #include "../../mqueuelist.h"
// #include "../../netkern.h"
// #include "../../filedb.h"
// #include "../../m_chat.h"
// #include "../../m_search.h"
// #include "../../sha.h"
// #include "../../rsa/rsa.h"

namespace waste {

namespace fs = std::filesystem;

// Simulation data for a peer connection
struct SimulatedPeer {
    PeerInfo info;
    std::chrono::steady_clock::time_point stateChangeTime;
    int statePhase = 0;  // 0=connecting, 1=authenticating, 2=online or failed
    bool shouldFail = false;  // Simulate random failures
};

// Simulation data for a transfer
struct SimulatedTransfer {
    int id;
    uint64_t totalSize;
    uint64_t transferred;
    bool paused = false;
    float simulatedSpeed;  // KB/s
};

// Implementation details hidden from TUI
struct WasteCoreImpl {
    // Simulated state for now
    std::vector<PeerInfo> peers;
    std::vector<SearchResult> searchResults;
    std::vector<TransferInfo> transfers;
    std::vector<ChatMessage> chatMessages;

    // Simulation tracking
    std::vector<SimulatedPeer> simulatedPeers;
    std::vector<SimulatedTransfer> simulatedTransfers;
    int nextTransferId = 1;

    // Shared directories
    std::vector<std::string> sharedDirs;
    bool scanningFiles = false;
    std::chrono::steady_clock::time_point lastScanTime;

    // Browse state
    std::string browsingPeer;
    std::string browsePath;

    bool initialized = false;
    bool simulationMode = true;  // Enable simulation when not connected to real core
};

WasteCore::WasteCore()
    : impl_(std::make_unique<WasteCoreImpl>()) {
    // Seed random for simulation
    std::srand(static_cast<unsigned>(std::time(nullptr)));
}

WasteCore::~WasteCore() {
    shutdown();
}

bool WasteCore::keysExist(const std::string& configDir) const {
    // WASTE expects key at {configDir}.pr4
    fs::path keyPath = fs::path(configDir + ".pr4");
    bool exists = fs::exists(keyPath);
    debug_printf("[KEYS] Checking for key at: %s -> %s\n",
                 keyPath.c_str(), exists ? "EXISTS" : "NOT FOUND");
    return exists;
}

bool WasteCore::generateKeys(const std::string& configDir) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Ensure config directory exists
    fs::create_directories(configDir);

    // WASTE expects key at {configDir}.pr4
    fs::path keyPath = fs::path(configDir + ".pr4");

    // Initialize random number generator
    R_RANDOM_STRUCT randomStruct;
    R_RandomInit(&randomStruct);

    // Seed with system entropy from /dev/urandom (best source on Linux)
    unsigned char seed[256];
    FILE* urandom = fopen("/dev/urandom", "rb");
    if (urandom) {
        size_t bytesRead = fread(seed, 1, 256, urandom);
        fclose(urandom);
        if (bytesRead != 256) {
            // Fallback: fill remaining with time-based entropy
            for (size_t i = bytesRead; i < 256; i++) {
                seed[i] = (unsigned char)(rand() ^ (GetTickCount() >> (i % 8)));
            }
        }
    } else {
        // Fallback if /dev/urandom not available (shouldn't happen on Linux)
        for (int i = 0; i < 256; i++) {
            seed[i] = (unsigned char)(rand() ^ (GetTickCount() >> (i % 8)));
        }
    }
    R_RandomUpdate(&randomStruct, seed, 256);

    // Generate 2048-bit RSA key pair
    R_RSA_PROTO_KEY protoKey;
    protoKey.bits = 2048;
    protoKey.useFermat4 = 1;

    R_RSA_PUBLIC_KEY pubKey;
    R_RSA_PRIVATE_KEY privKey;
    memset(&pubKey, 0, sizeof(pubKey));
    memset(&privKey, 0, sizeof(privKey));

    if (R_GeneratePEMKeys(&pubKey, &privKey, &protoKey, &randomStruct) != 0) {
        R_RandomFinal(&randomStruct);
        return false;
    }

    // Create password hash (empty password for now - can add password later)
    unsigned char passhash[SHA_OUTSIZE];
    SHAify sha;
    sha.add((unsigned char*)"", 0);  // Empty password
    sha.final(passhash);

    // Write private key to file
    if (writePrivateKey(keyPath.c_str(), &privKey, &randomStruct, passhash) != 0) {
        R_RandomFinal(&randomStruct);
        return false;
    }

    // Copy to global key and compute public key hash
    memcpy(&g_key, &privKey, sizeof(R_RSA_PRIVATE_KEY));
    SHAify m;
    m.add((unsigned char*)g_key.modulus, MAX_RSA_MODULUS_LEN);
    m.add((unsigned char*)g_key.publicExponent, MAX_RSA_MODULUS_LEN);
    m.final(g_pubkeyhash);

    // Clean up
    memset(&privKey, 0, sizeof(privKey));
    memset(&pubKey, 0, sizeof(pubKey));
    R_RandomFinal(&randomStruct);

    return true;
}

bool WasteCore::importKeys(const std::string& keyFilePath, const std::string& configDir) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!fs::exists(keyFilePath)) {
        return false;
    }

    // Ensure config directory exists
    fs::create_directories(configDir);

    // WASTE expects key at {configDir}.pr4
    fs::path destPath = fs::path(configDir + ".pr4");
    try {
        fs::copy_file(keyFilePath, destPath, fs::copy_options::overwrite_existing);
        return true;
    } catch (...) {
        return false;
    }
}

std::string WasteCore::getPublicKeyHash() const {
    std::lock_guard<std::mutex> lock(mutex_);
    // Convert public key hash to hex string
    char hexstr[SHA_OUTSIZE * 2 + 1];
    for (int i = 0; i < SHA_OUTSIZE; i++) {
        sprintf(hexstr + i*2, "%02X", g_pubkeyhash[i]);
    }
    hexstr[SHA_OUTSIZE * 2] = '\0';
    return std::string(hexstr);
}

std::vector<KeyInfo> WasteCore::getTrustedKeys() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<KeyInfo> keys;
    for (int i = 0; i < g_pklist.GetSize(); i++) {
        PKitem* pk = g_pklist.Get(i);
        if (!pk) continue;

        KeyInfo info;
        info.name = std::string(pk->name, strnlen(pk->name, sizeof(pk->name)));
        // Convert hash to hex string
        char hexstr[SHA_OUTSIZE * 2 + 1];
        for (int j = 0; j < SHA_OUTSIZE; j++) {
            sprintf(hexstr + j*2, "%02X", pk->hash[j]);
        }
        hexstr[SHA_OUTSIZE * 2] = '\0';
        info.fingerprint = std::string(hexstr);
        info.bits = pk->pk.bits;
        info.isPending = false;
        keys.push_back(info);
    }
    return keys;
}

std::vector<KeyInfo> WasteCore::getPendingKeys() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<KeyInfo> keys;
    for (int i = 0; i < g_pklist_pending.GetSize(); i++) {
        PKitem* pk = g_pklist_pending.Get(i);
        if (!pk) continue;

        KeyInfo info;
        info.name = std::string(pk->name, strnlen(pk->name, sizeof(pk->name)));
        // Convert hash to hex string
        char hexstr[SHA_OUTSIZE * 2 + 1];
        for (int j = 0; j < SHA_OUTSIZE; j++) {
            sprintf(hexstr + j*2, "%02X", pk->hash[j]);
        }
        hexstr[SHA_OUTSIZE * 2] = '\0';
        info.fingerprint = std::string(hexstr);
        info.bits = pk->pk.bits;
        info.isPending = true;
        keys.push_back(info);
    }
    return keys;
}

void WasteCore::trustPendingKey(int index) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (index < 0 || index >= g_pklist_pending.GetSize()) return;

    PKitem* pk = g_pklist_pending.Get(index);
    if (!pk) return;

    // Copy to trusted list
    PKitem newItem = *pk;
    g_pklist.Add(&newItem);

    // Remove from pending
    g_pklist_pending.Del(index);

    // Save the updated keyring
    savePKList();
}

void WasteCore::removeKey(int index, bool isPending) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (isPending) {
        if (index >= 0 && index < g_pklist_pending.GetSize()) {
            g_pklist_pending.Del(index);
        }
    } else {
        if (index >= 0 && index < g_pklist.GetSize()) {
            g_pklist.Del(index);
            savePKList();  // Only save when modifying trusted list
        }
    }
}

void WasteCore::updatePeerNickname(const std::string& address, const std::string& nickname) {
    // Note: This is called from main_MsgCallback which runs inside eventLoop's
    // mutex_ lock (eventLoop -> processMessages -> g_mql->run -> main_MsgCallback).
    // Do NOT acquire mutex_ here - it would deadlock since std::mutex is non-recursive.
    // The caller (eventLoop) already holds the lock.
    int foundIndex = -1;

    if (!impl_) return;

    for (size_t i = 0; i < impl_->peers.size(); i++) {
        auto& peer = impl_->peers[i];
        if (peer.address == address && peer.nickname != nickname) {
            peer.nickname = nickname;
            foundIndex = i;
            debug_printf("[PING] Updated peer %s nickname to '%s'\n",
                         address.c_str(), nickname.c_str());
            break;
        }
    }

    // Notify UI - post() and refresh() are thread-safe and don't acquire mutex_
    if (foundIndex >= 0 && onPeerNicknameChanged) {
        onPeerNicknameChanged(address, nickname);
    }
}

// Base64 encoding table
static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string base64_encode(const unsigned char* data, size_t len) {
    std::string result;
    result.reserve((len + 2) / 3 * 4);

    for (size_t i = 0; i < len; i += 3) {
        unsigned int n = ((unsigned int)data[i]) << 16;
        if (i + 1 < len) n |= ((unsigned int)data[i + 1]) << 8;
        if (i + 2 < len) n |= data[i + 2];

        result += b64_table[(n >> 18) & 0x3F];
        result += b64_table[(n >> 12) & 0x3F];
        result += (i + 1 < len) ? b64_table[(n >> 6) & 0x3F] : '=';
        result += (i + 2 < len) ? b64_table[n & 0x3F] : '=';
    }
    return result;
}

static int b64_decode_char(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static std::vector<unsigned char> base64_decode(const std::string& encoded) {
    std::vector<unsigned char> result;
    result.reserve(encoded.size() * 3 / 4);

    unsigned int buf = 0;
    int bits = 0;

    for (char c : encoded) {
        if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
        int val = b64_decode_char(c);
        if (val < 0) continue;

        buf = (buf << 6) | val;
        bits += 6;

        if (bits >= 8) {
            bits -= 8;
            result.push_back((buf >> bits) & 0xFF);
        }
    }
    return result;
}

std::string WasteCore::getDefaultExportPath() const {
    std::string name = nickname_.empty() ? "mykey" : nickname_;
    // Sanitize name for filename
    for (char& c : name) {
        if (!isalnum(c) && c != '-' && c != '_') c = '_';
    }
    return configDir_ + "/" + name + ".wastekey";
}

bool WasteCore::exportPublicKey(const std::string& filepath) const {
    std::lock_guard<std::mutex> lock(mutex_);

    // Get public key from our private key
    R_RSA_PUBLIC_KEY pubkey;
    pubkey.bits = g_key.bits;
    memcpy(pubkey.modulus, g_key.modulus, MAX_RSA_MODULUS_LEN);
    memcpy(pubkey.exponent, g_key.publicExponent, MAX_RSA_MODULUS_LEN);

    // Build the key data structure to export
    // Format: [20 bytes hash][16 bytes name][4 bytes bits][256 bytes modulus][256 bytes exponent]
    std::vector<unsigned char> keydata;
    keydata.reserve(20 + 16 + 4 + MAX_RSA_MODULUS_LEN * 2);

    // Hash (20 bytes)
    for (int i = 0; i < SHA_OUTSIZE; i++) {
        keydata.push_back(g_pubkeyhash[i]);
    }

    // Name (16 bytes, null-padded)
    std::string name = nickname_;
    if (name.length() > 15) name = name.substr(0, 15);
    for (size_t i = 0; i < 16; i++) {
        keydata.push_back(i < name.length() ? name[i] : 0);
    }

    // Bits (4 bytes, little-endian)
    keydata.push_back(pubkey.bits & 0xFF);
    keydata.push_back((pubkey.bits >> 8) & 0xFF);
    keydata.push_back((pubkey.bits >> 16) & 0xFF);
    keydata.push_back((pubkey.bits >> 24) & 0xFF);

    // Modulus (256 bytes)
    for (int i = 0; i < MAX_RSA_MODULUS_LEN; i++) {
        keydata.push_back(pubkey.modulus[i]);
    }

    // Exponent (256 bytes)
    for (int i = 0; i < MAX_RSA_MODULUS_LEN; i++) {
        keydata.push_back(pubkey.exponent[i]);
    }

    // Encode to base64
    std::string b64 = base64_encode(keydata.data(), keydata.size());

    // Format with line breaks (64 chars per line)
    std::string formatted;
    for (size_t i = 0; i < b64.length(); i += 64) {
        formatted += b64.substr(i, 64) + "\n";
    }

    // Build the PEM-like output
    std::ofstream out(filepath);
    if (!out) {
        debug_printf("[KEYS] Failed to open %s for writing\n", filepath.c_str());
        return false;
    }

    // Convert hash to hex for display
    char hashHex[SHA_OUTSIZE * 2 + 1];
    for (int i = 0; i < SHA_OUTSIZE; i++) {
        sprintf(hashHex + i*2, "%02X", g_pubkeyhash[i]);
    }
    hashHex[SHA_OUTSIZE * 2] = '\0';

    out << "-----BEGIN WASTE PUBLIC KEY-----\n";
    out << "Name: " << (nickname_.empty() ? "(unnamed)" : nickname_) << "\n";
    out << "Hash: " << hashHex << "\n";
    out << "Bits: " << pubkey.bits << "\n";
    out << "\n";
    out << formatted;
    out << "-----END WASTE PUBLIC KEY-----\n";

    out.close();

    debug_printf("[KEYS] Exported public key to %s\n", filepath.c_str());
    return true;
}

bool WasteCore::importPublicKey(const std::string& filepath) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::ifstream in(filepath);
    if (!in) {
        debug_printf("[KEYS] Failed to open %s for reading\n", filepath.c_str());
        return false;
    }

    std::string line;
    bool inKey = false;
    std::string b64data;
    std::string importName;

    while (std::getline(in, line)) {
        // Trim whitespace
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ')) {
            line.pop_back();
        }

        if (line == "-----BEGIN WASTE PUBLIC KEY-----") {
            inKey = true;
            continue;
        }
        if (line == "-----END WASTE PUBLIC KEY-----") {
            break;
        }
        if (!inKey) continue;

        // Parse header fields
        if (line.substr(0, 6) == "Name: ") {
            importName = line.substr(6);
            continue;
        }
        if (line.substr(0, 6) == "Hash: " || line.substr(0, 6) == "Bits: ") {
            continue;  // Skip these, we'll compute hash from key data
        }
        if (line.empty()) continue;

        // Accumulate base64 data
        b64data += line;
    }

    in.close();

    if (b64data.empty()) {
        debug_printf("[KEYS] No key data found in %s\n", filepath.c_str());
        return false;
    }

    // Decode base64
    std::vector<unsigned char> keydata = base64_decode(b64data);

    // Expected size: 20 + 16 + 4 + 256 + 256 = 552 bytes
    if (keydata.size() < 552) {
        debug_printf("[KEYS] Key data too short: %zu bytes (expected 552)\n", keydata.size());
        return false;
    }

    // Parse the key data
    PKitem newKey;
    memset(&newKey, 0, sizeof(newKey));

    // Hash (20 bytes)
    memcpy(newKey.hash, keydata.data(), SHA_OUTSIZE);

    // Name (16 bytes)
    memcpy(newKey.name, keydata.data() + 20, 16);
    newKey.name[15] = '\0';  // Ensure null termination

    // Override name if provided in header
    if (!importName.empty() && importName != "(unnamed)") {
        strncpy(newKey.name, importName.c_str(), 15);
        newKey.name[15] = '\0';
    }

    // Bits (4 bytes, little-endian)
    newKey.pk.bits = keydata[36] | (keydata[37] << 8) | (keydata[38] << 16) | (keydata[39] << 24);

    // Modulus (256 bytes)
    memcpy(newKey.pk.modulus, keydata.data() + 40, MAX_RSA_MODULUS_LEN);

    // Exponent (256 bytes)
    memcpy(newKey.pk.exponent, keydata.data() + 40 + MAX_RSA_MODULUS_LEN, MAX_RSA_MODULUS_LEN);

    // Verify hash matches the public key
    unsigned char computedHash[SHA_OUTSIZE];
    SHAify sha;
    sha.add(newKey.pk.modulus, MAX_RSA_MODULUS_LEN);
    sha.add(newKey.pk.exponent, MAX_RSA_MODULUS_LEN);
    sha.final(computedHash);

    if (memcmp(computedHash, newKey.hash, SHA_OUTSIZE) != 0) {
        debug_printf("[KEYS] Hash mismatch - key data may be corrupted\n");
        // Use computed hash instead
        memcpy(newKey.hash, computedHash, SHA_OUTSIZE);
    }

    // Check if we already have this key
    for (int i = 0; i < g_pklist.GetSize(); i++) {
        PKitem* existing = g_pklist.Get(i);
        if (existing && memcmp(existing->hash, newKey.hash, SHA_OUTSIZE) == 0) {
            debug_printf("[KEYS] Key already in trusted list\n");
            return true;  // Already have it, not an error
        }
    }

    // Check if it's our own key
    if (memcmp(newKey.hash, g_pubkeyhash, SHA_OUTSIZE) == 0) {
        debug_printf("[KEYS] Cannot import own key\n");
        return false;
    }

    // Add to trusted list
    g_pklist.Add(&newKey);
    savePKList();

    char hashHex[SHA_OUTSIZE * 2 + 1];
    for (int i = 0; i < SHA_OUTSIZE; i++) {
        sprintf(hashHex + i*2, "%02X", newKey.hash[i]);
    }
    hashHex[SHA_OUTSIZE * 2] = '\0';

    debug_printf("[KEYS] Imported key: name='%s' hash=%s bits=%u\n",
                 newKey.name, hashHex, newKey.pk.bits);

    return true;
}

WasteCore::InitResult WasteCore::initialize(const std::string& configDir,
                                             int listenPort,
                                             const std::string& networkName) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (running_) {
        return InitResult::Error;
    }

    configDir_ = configDir;
    listenPort_ = listenPort;
    networkName_ = networkName;
    g_port = listenPort;

    // Set config path prefix
    strncpy(g_config_prefix, configDir.c_str(), sizeof(g_config_prefix) - 1);
    g_config_prefix[sizeof(g_config_prefix) - 1] = '\0';

    // Initialize config file (used by various WASTE components)
    char configPath[1024];
    snprintf(configPath, sizeof(configPath), "%s.pr0", g_config_prefix);
    g_config = new C_Config(configPath);

    // Initialize the global random number generator (critical for unique GUIDs!)
    MYSRAND();
    debug_printf("[INIT] Initialized g_random from /dev/urandom\n");

    // Register this instance for callbacks
    g_waste_core_instance = this;

    // Simulation mode: port 0 means skip key checks and just run simulation
    bool simulationOnly = (listenPort == 0);

    if (!simulationOnly) {
        // Check for keys
        if (!keysExist(configDir)) {
            return InitResult::NoKeys;
        }

        // Load keys using WASTE's reloadKey function
        fs::path keyPath = fs::path(configDir) / "keys" / "waste.key";
        // Note: reloadKey expects a password, empty string for unencrypted
        reloadKey((char*)"");

        // Check if key loaded successfully
        if (g_key.bits == 0) {
            return InitResult::KeyLoadError;
        }

        // Load public key list (trusted peers)
        int numKeys = loadPKList();
        debug_printf("Loaded %d public keys from keyring\n", numKeys);

        // Set up network hash for private network
        if (!networkName.empty()) {
            SHAify sha;
            sha.add((unsigned char*)networkName.c_str(), networkName.length());
            sha.final(g_networkhash);
            g_use_networkhash = 1;
        }

        // Initialize WASTE components
        g_dns = new C_AsyncDNS();
        g_mql = new C_MessageQueueList(main_MsgCallback, 6);

        if (listenPort > 0) {
            g_listen = new C_Listen((short)listenPort);
            if (g_listen->is_error()) {
                delete g_listen;
                g_listen = nullptr;
                delete g_mql;
                g_mql = nullptr;
                delete g_dns;
                g_dns = nullptr;
                return InitResult::ListenError;
            }
        }

        // Generate client ID
        CreateID128(&g_client_id);
        MakeID128Str(&g_client_id, g_client_id_str);

        // Initialize file database
        g_database = new C_FileDB();
        g_database->UpdateExtList(g_def_extlist);

        impl_->simulationMode = false;
    } else {
        impl_->simulationMode = true;
    }

    impl_->initialized = true;

    // Start event loop thread
    shouldStop_ = false;
    running_ = true;
    eventThread_ = std::thread(&WasteCore::eventLoop, this);

    return InitResult::Success;
}

void WasteCore::shutdown() {
    if (!running_) {
        return;
    }

    shouldStop_ = true;

    if (eventThread_.joinable()) {
        eventThread_.join();
    }

    running_ = false;

    std::lock_guard<std::mutex> lock(mutex_);

    // Clean up active transfers
    while (g_sends.GetSize() > 0) {
        delete g_sends.Get(0);
        g_sends.Del(0);
    }
    while (g_recvs.GetSize() > 0) {
        delete g_recvs.Get(0);
        g_recvs.Del(0);
    }

    // Clean up WASTE components
    // Note: g_newdatabase might equal g_database after a completed scan, so check first
    if (g_newdatabase && g_newdatabase != g_database) {
        delete g_newdatabase;
        g_newdatabase = nullptr;
    }
    if (g_database) {
        delete g_database;
        g_database = nullptr;
    }
    if (g_listen) {
        delete g_listen;
        g_listen = nullptr;
    }
    if (g_mql) {
        delete g_mql;
        g_mql = nullptr;
    }
    if (g_dns) {
        delete g_dns;
        g_dns = nullptr;
    }
    if (g_config) {
        delete g_config;
        g_config = nullptr;
    }

    g_waste_core_instance = nullptr;
    impl_->initialized = false;
}

void WasteCore::eventLoop() {
    auto lastSimUpdate = std::chrono::steady_clock::now();

    while (!shouldStop_) {
        auto now = std::chrono::steady_clock::now();

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (impl_->initialized) {
                processMessages();

                // Run simulation updates every ~100ms
                if (impl_->simulationMode) {
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - lastSimUpdate).count();
                    if (elapsed >= 100) {
                        processSimulation(elapsed);
                        lastSimUpdate = now;
                    }
                }

                updateStats();
            }
        }

        // Short sleep to avoid busy-waiting while maintaining good throughput
        // 5ms allows ~200 iterations/sec for responsive transfers
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

void WasteCore::processMessages() {
    if (!impl_->simulationMode && g_mql) {
        // Process network connections
        NetKern_Run();

        // Process message queue (1 = do routing)
        g_mql->run(g_route_traffic);

        // Update peer list from actual connections
        updatePeerListFromConnections();

        // Process file transfers
        processTransfers();

        // Process file database scanning
        if (g_newdatabase && impl_->scanningFiles) {
            int result = g_newdatabase->DoScan(50, g_database);  // Max 50ms per call
            debug_printf("[SCAN] DoScan returned %d\n", result);

            // DoScan returns:
            // -1: scan stack is NULL (no scan initiated or already completed)
            // >=0: number of files found (scan in progress or just finished)
            //
            // When scan completes WITHIN a DoScan call, it deletes the stack and returns
            // the file count. Only on the NEXT call does it return -1.
            // So we check for -1 OR try another call to detect immediate completion.

            bool scanDone = (result < 0);
            if (!scanDone && result >= 0) {
                // Scan might have just completed - try one more call to check
                int result2 = g_newdatabase->DoScan(1, g_database);
                if (result2 < 0) {
                    scanDone = true;
                    debug_printf("[SCAN] Scan completed (detected on second check)\n");
                }
            }

            if (scanDone) {
                // Scanning complete - swap databases
                int numFiles = g_newdatabase->GetNumFiles();
                debug_printf("[SCAN] Scan complete, found %d files\n", numFiles);
                if (g_database && g_database != g_newdatabase) {
                    delete g_database;
                }
                g_database = g_newdatabase;
                g_newdatabase = nullptr;
                impl_->scanningFiles = false;
            }
        }
    }
}

void WasteCore::processTransfers() {
    if (!g_mql) return;

    // Process active uploads (XferSend)
    for (int x = 0; x < g_sends.GetSize(); x++) {
        XferSend* send = g_sends.Get(x);
        if (!send) continue;

        // Process header if needed
        int headerResult = send->run_hdr(g_mql);
        if (headerResult) {
            // Transfer finished (completed or error)
            char* err = send->GetError();
            debug_printf("[XFER] Upload finished: %s - %s\n",
                send->GetName(), err ? err : "completed");

            bool completed = err && strstr(err, "Completed");

            // Report final progress (100%) before status change
            if (completed && onTransferProgress) {
                unsigned int sizeLow, sizeHigh;
                send->GetSize(&sizeLow, &sizeHigh);
                uint64_t totalSize = ((uint64_t)sizeHigh << 32) | sizeLow;
                onTransferProgress((int)(intptr_t)send, totalSize, totalSize, 0);
            }

            // Notify TUI of completion
            if (onTransferStatusChanged) {
                onTransferStatusChanged((int)(intptr_t)send,
                    completed ? TransferStatus::Completed : TransferStatus::Failed,
                    err ? err : "");
            }

            delete send;
            g_sends.Del(x--);
            continue;
        }

        // Send data chunks
        send->run(g_mql);

        // Report upload progress
        if (onTransferProgress) {
            unsigned int chunksTotal = send->getChunksTotal();
            unsigned int maxChunkSent = send->getMaxChunkSent();
            if (chunksTotal > 0) {
                // Estimate transferred bytes (maxChunkSent is highest chunk index sent)
                uint64_t transferred = (uint64_t)(maxChunkSent + 1) * FILE_CHUNKSIZE;
                unsigned int sizeLow, sizeHigh;
                send->GetSize(&sizeLow, &sizeHigh);
                uint64_t totalSize = ((uint64_t)sizeHigh << 32) | sizeLow;
                float speedKBps = send->getSpeedCps() / 1024.0f;
                onTransferProgress((int)(intptr_t)send, transferred, totalSize, speedKBps);
            }
        }
    }

    // Process active downloads (XferRecv)
    for (int x = 0; x < g_recvs.GetSize(); x++) {
        XferRecv* recv = g_recvs.Get(x);
        if (!recv) continue;

        int result = recv->run(g_mql);
        if (result) {
            // Transfer finished
            char* err = recv->GetError();
            debug_printf("[XFER] Download finished: %s\n", err ? err : "completed");

            bool completed = !err || (err && strstr(err, "Completed"));

            // Report final progress (100%) before status change
            if (completed && onTransferProgress) {
                uint64_t totalSize = ((uint64_t)recv->getBytesTotalHigh() << 32) | recv->getBytesTotalLow();
                onTransferProgress((int)(intptr_t)recv, totalSize, totalSize, 0);
            }

            // Notify TUI of completion
            if (onTransferStatusChanged) {
                onTransferStatusChanged((int)(intptr_t)recv,
                    completed ? TransferStatus::Completed : TransferStatus::Failed,
                    err ? err : "");
            }

            delete recv;
            g_recvs.Del(x--);
            continue;
        }

        // Report download progress
        if (onTransferProgress) {
            unsigned int chunkCount = recv->getChunkCount();
            unsigned int chunkTotal = recv->getChunkTotal();
            if (chunkTotal > 0) {
                // Calculate transferred bytes
                uint64_t transferred = (uint64_t)chunkCount * FILE_CHUNKSIZE;
                // Get total size from the recv object
                uint64_t totalSize = ((uint64_t)recv->getBytesTotalHigh() << 32) | recv->getBytesTotalLow();
                float speedKBps = recv->getSpeedCps() / 1024.0f;
                onTransferProgress((int)(intptr_t)recv, transferred, totalSize, speedKBps);
            }
        }
    }
}

void WasteCore::updatePeerListFromConnections() {
    if (!g_mql) return;

    // Sync peer list with actual message queues
    int numQueues = g_mql->GetNumQueues();

    // For each queue, check if we have it in our peer list
    for (int i = 0; i < numQueues; i++) {
        C_MessageQueue* q = g_mql->GetQueue(i);
        if (!q) continue;

        C_Connection* conn = q->get_con();
        if (!conn) continue;

        // Get connection info
        unsigned long remoteIP = conn->get_remote();
        short remotePort = conn->get_remote_port();

        // Convert IP to string
        struct in_addr addr;
        addr.s_addr = remoteIP;
        std::string ipStr = inet_ntoa(addr);

        // Find or add peer
        bool found = false;
        for (auto& peer : impl_->peers) {
            if (peer.address == ipStr && peer.port == remotePort) {
                found = true;
                // If we have a queue with a valid connection, it's online
                if (peer.status != ConnectionStatus::Online) {
                    peer.status = ConnectionStatus::Online;
                    if (onPeerStatusChanged) {
                        int idx = &peer - &impl_->peers[0];
                        onPeerStatusChanged(idx, ConnectionStatus::Online, "");
                    }
                }
                break;
            }
        }

        // If not found, add it
        if (!found) {
            PeerInfo peer;
            peer.address = ipStr;
            peer.port = remotePort;
            peer.status = ConnectionStatus::Online;
            peer.connectedAt = std::chrono::steady_clock::now();
            peer.filesShared = 0;
            impl_->peers.push_back(peer);

            if (onPeerConnected) {
                onPeerConnected(peer);
            }
        }
    }
}

void WasteCore::updateStats() {
    // Calculate network stats from simulation or real data
    if (onNetworkStatsUpdated && impl_->simulationMode) {
        NetworkStats stats;
        stats.connectedPeers = 0;
        stats.uploadKBps = 0;
        stats.downloadKBps = 0;

        // Count online peers
        for (const auto& sp : impl_->simulatedPeers) {
            if (sp.info.status == ConnectionStatus::Online) {
                stats.connectedPeers++;
            }
        }

        // Sum transfer speeds
        for (const auto& st : impl_->simulatedTransfers) {
            if (!st.paused && st.transferred < st.totalSize) {
                stats.downloadKBps += st.simulatedSpeed;
            }
        }

        onNetworkStatsUpdated(stats);
    }
}

void WasteCore::processSimulation(int64_t elapsedMs) {
    auto now = std::chrono::steady_clock::now();

    // Process peer connection state changes
    for (auto& sp : impl_->simulatedPeers) {
        auto timeSinceChange = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - sp.stateChangeTime).count();

        if (sp.statePhase == 0 && timeSinceChange >= 800) {
            // Connecting -> Authenticating (after 0.8s)
            sp.statePhase = 1;
            sp.info.status = ConnectionStatus::Authenticating;
            sp.stateChangeTime = now;

            if (onPeerStatusChanged) {
                // Find index
                for (size_t i = 0; i < impl_->simulatedPeers.size(); i++) {
                    if (&impl_->simulatedPeers[i] == &sp) {
                        onPeerStatusChanged(i, sp.info.status, "");
                        break;
                    }
                }
            }
        }
        else if (sp.statePhase == 1 && timeSinceChange >= 1200) {
            // Authenticating -> Online or Failed (after 1.2s)
            sp.statePhase = 2;

            if (sp.shouldFail) {
                sp.info.status = ConnectionStatus::Failed;
                sp.info.errorMsg = "(auth failed)";
            } else {
                sp.info.status = ConnectionStatus::Online;
                sp.info.connectedAt = now;
                // Assign random file count and nickname for display
                sp.info.filesShared = 100 + (rand() % 2000);
                if (sp.info.nickname.empty()) {
                    static const char* names[] = {"peer", "user", "node", "friend"};
                    sp.info.nickname = names[rand() % 4] + std::to_string(rand() % 100);
                }
            }

            if (onPeerStatusChanged) {
                for (size_t i = 0; i < impl_->simulatedPeers.size(); i++) {
                    if (&impl_->simulatedPeers[i] == &sp) {
                        onPeerStatusChanged(i, sp.info.status, sp.info.errorMsg);
                        break;
                    }
                }
            }
        }
    }

    // Process transfer progress
    for (auto& st : impl_->simulatedTransfers) {
        if (st.paused || st.transferred >= st.totalSize) {
            continue;
        }

        // Calculate bytes transferred in this interval
        uint64_t bytesThisInterval = (uint64_t)(st.simulatedSpeed * 1024 * elapsedMs / 1000);
        st.transferred = (std::min)(st.totalSize, st.transferred + bytesThisInterval);

        if (onTransferProgress) {
            onTransferProgress(st.id, st.transferred, st.totalSize, st.simulatedSpeed);
        }

        // Check if completed
        if (st.transferred >= st.totalSize && onTransferStatusChanged) {
            onTransferStatusChanged(st.id, TransferStatus::Completed, "");
        }
    }
}

// Connection management
void WasteCore::connectToPeer(const std::string& address, int port) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Add peer to our list
    PeerInfo peer;
    peer.address = address;
    peer.port = port;
    peer.status = ConnectionStatus::Connecting;
    peer.connectedAt = std::chrono::steady_clock::now();
    peer.filesShared = 0;

    impl_->peers.push_back(peer);

    if (!impl_->simulationMode) {
        // Real WASTE connection
        struct in_addr addr;
        unsigned long ip;
        if (safe_inet_pton(address.c_str(), &addr)) {
            ip = addr.s_addr;
        } else {
            ip = INADDR_NONE;
            // Need DNS resolution - the connection will handle this
        }
        NetKern_ConnectToHostIfOK(ip, port);
    } else {
        // Simulation mode - create simulated peer
        SimulatedPeer sp;
        sp.info = peer;
        sp.stateChangeTime = std::chrono::steady_clock::now();
        sp.statePhase = 0;
        sp.shouldFail = (rand() % 5 == 0);  // 20% failure rate
        impl_->simulatedPeers.push_back(sp);
    }

    if (onPeerConnected) {
        onPeerConnected(peer);
    }
}

void WasteCore::disconnectPeer(int index) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (index < 0 || index >= (int)impl_->peers.size()) {
        return;
    }

    auto& peer = impl_->peers[index];

    if (!impl_->simulationMode && g_mql) {
        // Find the queue matching this peer's IP/port and close it
        for (int i = 0; i < g_mql->GetNumQueues(); i++) {
            C_MessageQueue* q = g_mql->GetQueue(i);
            if (!q) continue;

            C_Connection* conn = q->get_con();
            if (!conn) continue;

            // Match by IP and port
            struct in_addr addr;
            addr.s_addr = conn->get_remote();
            std::string connIP = inet_ntoa(addr);
            int connPort = conn->get_remote_port();

            if (connIP == peer.address && connPort == peer.port) {
                // Close the connection - it will be cleaned up in the next run() cycle
                conn->close();
                break;
            }
        }
    }

    // Also remove from simulated peers if present
    if (impl_->simulationMode) {
        impl_->simulatedPeers.erase(
            std::remove_if(impl_->simulatedPeers.begin(),
                           impl_->simulatedPeers.end(),
                           [&peer](const SimulatedPeer& sp) {
                               return sp.info.address == peer.address &&
                                      sp.info.port == peer.port;
                           }),
            impl_->simulatedPeers.end());
    }

    impl_->peers.erase(impl_->peers.begin() + index);

    if (onPeerDisconnected) {
        onPeerDisconnected(index);
    }
}

void WasteCore::retryConnection(int index) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (index < 0 || index >= (int)impl_->peers.size()) {
        return;
    }

    auto& peer = impl_->peers[index];
    if (peer.status == ConnectionStatus::Failed) {
        peer.status = ConnectionStatus::Connecting;
        peer.errorMsg.clear();

        if (!impl_->simulationMode) {
            // Initiate real WASTE connection
            struct in_addr addr;
            if (safe_inet_pton(peer.address.c_str(), &addr)) {
                NetKern_ConnectToHostIfOK(addr.s_addr, peer.port);
            }
        } else {
            // Reset simulation state for this peer
            for (auto& sp : impl_->simulatedPeers) {
                if (sp.info.address == peer.address && sp.info.port == peer.port) {
                    sp.statePhase = 0;
                    sp.stateChangeTime = std::chrono::steady_clock::now();
                    sp.shouldFail = (rand() % 5 == 0);  // 20% failure rate
                    sp.info.status = ConnectionStatus::Connecting;
                    break;
                }
            }
        }

        if (onPeerStatusChanged) {
            onPeerStatusChanged(index, peer.status, "");
        }
    }
}

int WasteCore::getPeerCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return impl_->peers.size();
}

// File sharing
void WasteCore::addSharedDirectory(const std::string& path) {
    {
        std::lock_guard<std::mutex> lock(mutex_);

        // Check if already shared
        for (const auto& dir : impl_->sharedDirs) {
            if (dir == path) return;
        }

        impl_->sharedDirs.push_back(path);

        // Trigger rescan
        rescanSharedDirectoriesInternal();
    }
    // Save config (outside lock since saveConfig also locks)
    saveConfig();
}

void WasteCore::removeSharedDirectory(int index) {
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (index < 0 || index >= (int)impl_->sharedDirs.size()) {
            return;
        }

        impl_->sharedDirs.erase(impl_->sharedDirs.begin() + index);

        // Trigger rescan
        rescanSharedDirectoriesInternal();
    }
    // Save config (outside lock since saveConfig also locks)
    saveConfig();
}

void WasteCore::rescanSharedDirectories() {
    std::lock_guard<std::mutex> lock(mutex_);
    rescanSharedDirectoriesInternal();
}

std::vector<std::string> WasteCore::getSharedDirectories() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return impl_->sharedDirs;
}

void WasteCore::rescanSharedDirectoriesInternal() {
    // Build semicolon-separated path list
    std::string pathList;
    for (size_t i = 0; i < impl_->sharedDirs.size(); i++) {
        if (i > 0) pathList += ";";
        pathList += impl_->sharedDirs[i];
    }

    debug_printf("[SCAN] Scanning directories: '%s'\n", pathList.c_str());

    if (!impl_->simulationMode) {
        // Create new database for scanning
        if (g_newdatabase && g_newdatabase != g_database) {
            delete g_newdatabase;
        }
        g_newdatabase = new C_FileDB();
        g_newdatabase->UpdateExtList(g_def_extlist);

        if (!pathList.empty()) {
            g_newdatabase->Scan((char*)pathList.c_str());
            impl_->scanningFiles = true;
            debug_printf("[SCAN] Started scanning, scanningFiles=true\n");
        } else {
            // No directories - just swap empty database
            if (g_database && g_database != g_newdatabase) {
                delete g_database;
            }
            g_database = g_newdatabase;
            g_newdatabase = nullptr;
            impl_->scanningFiles = false;
            debug_printf("[SCAN] No directories to scan\n");
        }
    }

    impl_->lastScanTime = std::chrono::steady_clock::now();
}

void WasteCore::search(const std::string& query) {
    debug_printf("[SEARCH] search() called with query='%s'\n", query.c_str());
    debug_printf("[SEARCH] simulationMode=%d, g_mql=%p, g_database=%p\n",
        impl_->simulationMode, (void*)g_mql, (void*)g_database);

    // Clear browse mode - this is a regular search
    {
        std::lock_guard<std::mutex> lock(g_browse_mutex);
        g_browse_path.clear();
    }

    std::lock_guard<std::mutex> lock(mutex_);

    if (!impl_->simulationMode && g_mql) {
        // First, search local database for our own shared files
        if (g_database && g_database->GetNumFiles() > 0) {
            debug_printf("[SEARCH] Searching local database (%d files) for '%s'\n",
                g_database->GetNumFiles(), query.c_str());

            // Iterate through database by position and match files
            for (int pos = 0; pos < g_database->GetNumFiles(); pos++) {
                char filename[2048];
                char meta[256];
                int sizeLow, sizeHigh, vIndex;

                // Use GetFileByPosition to iterate by array position
                if (g_database->GetFileByPosition(pos, filename, meta, &sizeLow, &sizeHigh, &vIndex) == 0) {
                    // Extract just the filename from the full path
                    char* fname = filename;
                    char* p = filename;
                    while (*p) {
                        if (*p == '/' || *p == '\\') fname = p + 1;
                        p++;
                    }

                    debug_printf("[SEARCH] Checking file: '%s' (vIndex=%d)\n", fname, vIndex);

                    // Case-insensitive substring search
                    std::string fnameStr = fname;
                    std::string queryLower = query;
                    std::transform(fnameStr.begin(), fnameStr.end(), fnameStr.begin(), ::tolower);
                    std::transform(queryLower.begin(), queryLower.end(), queryLower.begin(), ::tolower);

                    if (fnameStr.find(queryLower) != std::string::npos) {
                        debug_printf("[SEARCH] Local match: %s\n", fname);

                        if (onSearchResult) {
                            SearchResult result;
                            result.filename = fname;
                            result.size = ((uint64_t)(unsigned int)sizeHigh << 32) | (uint64_t)(unsigned int)sizeLow;
                            result.type = meta[0] ? meta : "";
                            result.sources = 1;
                            result.user = nickname_.empty() ? "local" : nickname_;
                            // Use our client ID and v_index for local files (v_index is what GetFile expects)
                            result.hash = std::string(g_client_id_str) + ":" + std::to_string(vIndex);

                            onSearchResult(result);
                        }
                    }
                } else {
                    debug_printf("[SEARCH] GetFileByPosition(%d) failed\n", pos);
                }
            }
        } else {
            debug_printf("[SEARCH] No local database or empty (%p, %d files)\n",
                (void*)g_database, g_database ? g_database->GetNumFiles() : 0);
        }

        // Send real WASTE search request to peers
        C_MessageSearchRequest req;
        req.set_min_conspeed(0);  // Accept all speeds
        req.set_searchstring((char*)query.c_str());

        T_Message msg = {0,};
        msg.data = req.Make();
        if (msg.data) {
            msg.message_type = MESSAGE_SEARCH;
            msg.message_length = msg.data->GetLength();

            g_mql->send(&msg);

            // Store the search GUID for tracking replies
            // NOTE: Must be AFTER send() because send() generates the GUID via CreateID128
            memcpy(&g_last_scanid, &msg.message_guid, sizeof(T_GUID));
            g_last_scanid_used = 1;
        }
    } else if (impl_->simulationMode) {
        // Simulate search results
        std::vector<std::string> fakeFiles = {
            query + "_compilation.zip",
            query + "_pack.rar",
            "Best of " + query + ".mp3",
            query + " - unreleased.flac"
        };

        for (size_t i = 0; i < fakeFiles.size(); i++) {
            if (onSearchResult) {
                SearchResult result;
                result.filename = fakeFiles[i];
                result.size = (10 + rand() % 100) * 1024 * 1024;  // 10-110 MB
                result.type = fakeFiles[i].substr(fakeFiles[i].rfind('.') + 1);
                result.sources = 1 + rand() % 5;
                static const char* names[] = {"alice", "bob", "charlie", "dave"};
                result.user = names[rand() % 4];
                result.hash = "hash" + std::to_string(rand());

                onSearchResult(result);
            }
        }

        if (onSearchComplete) {
            onSearchComplete();
        }
    }
}

void WasteCore::cancelSearch() {
    std::lock_guard<std::mutex> lock(mutex_);

    // Clear the scan ID so incoming SEARCH_REPLY messages are ignored
    g_last_scanid_used = 0;
    memset(&g_last_scanid, 0, sizeof(g_last_scanid));
    debug_printf("[SEARCH] Search cancelled\n");

    if (onSearchComplete) {
        onSearchComplete();
    }
}

// Transfers
void WasteCore::downloadFile(const std::string& hash, const std::string& peer) {
    // Don't hold lock when creating XferRecv - it sends network messages
    if (!impl_->simulationMode && g_mql) {
        // hash is in format "GUID:index" from search result
        // peer contains filename from search

        // Get download path (defaults to current dir if not set)
        std::string downloadPath = g_download_path;
        if (downloadPath.empty()) {
            downloadPath = configDir_ + "/downloads";
            // Ensure directory exists
            std::error_code ec;
            std::filesystem::create_directories(downloadPath, ec);
        }

        // XferRecv expects: guididx, sizestr, filename, path
        // hash = "GUID:index", peer = filename from search result
        std::string guididx = hash;
        std::string filename = peer;  // Note: peer param is actually filename from search
        std::string sizestr = "";  // We don't have size string, but it's for display only

        debug_printf("[XFER] Starting download: guididx='%s' (len=%zu) filename='%s' path='%s'\n",
            guididx.c_str(), guididx.length(), filename.c_str(), downloadPath.c_str());

        // Verify guididx format (should be 32-char GUID + ":" + index, min 34 chars)
        if (guididx.length() < 34) {
            debug_printf("[XFER] ERROR: guididx too short (need at least 34 chars)\n");
            return;
        }

        XferRecv* recv = new XferRecv(g_mql,
            (char*)guididx.c_str(),
            (char*)sizestr.c_str(),
            (char*)filename.c_str(),
            (char*)downloadPath.c_str());

        char* err = recv->GetError();
        if (err) {
            debug_printf("[XFER] Download failed to start: %s\n", err);
            delete recv;
            return;
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            g_recvs.Add(recv);
        }

        // Create transfer info for UI
        TransferInfo xfer;
        xfer.id = (int)(intptr_t)recv;  // Use pointer as ID
        xfer.filename = filename;
        xfer.direction = TransferDirection::Download;
        xfer.status = TransferStatus::Active;
        xfer.totalSize = 0;  // Will be updated when header arrives
        xfer.transferred = 0;
        xfer.speedKBps = 0;
        xfer.peer = hash.substr(0, 32);  // First 32 chars are GUID

        if (onTransferAdded) {
            onTransferAdded(xfer);
        }
        return;
    }

    // Simulation mode fallback
    std::lock_guard<std::mutex> lock(mutex_);

    SimulatedTransfer st;
    st.id = impl_->nextTransferId++;
    st.totalSize = 10 * 1024 * 1024 + (rand() % (100 * 1024 * 1024));  // 10-110 MB
    st.transferred = 0;
    st.paused = false;
    st.simulatedSpeed = 200 + (rand() % 800);  // 200-1000 KB/s

    impl_->simulatedTransfers.push_back(st);

    // Create transfer info for UI
    TransferInfo xfer;
    xfer.id = st.id;
    xfer.filename = "file_" + hash.substr(0, 8) + ".bin";
    xfer.direction = TransferDirection::Download;
    xfer.status = TransferStatus::Active;
    xfer.totalSize = st.totalSize;
    xfer.transferred = 0;
    xfer.speedKBps = st.simulatedSpeed;
    xfer.peer = peer;

    impl_->transfers.push_back(xfer);

    if (onTransferAdded) {
        onTransferAdded(xfer);
    }
}

void WasteCore::pauseTransfer(int id) {
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto& st : impl_->simulatedTransfers) {
        if (st.id == id) {
            st.paused = true;
            if (onTransferStatusChanged) {
                onTransferStatusChanged(id, TransferStatus::Paused, "");
            }
            break;
        }
    }
}

void WasteCore::resumeTransfer(int id) {
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto& st : impl_->simulatedTransfers) {
        if (st.id == id) {
            st.paused = false;
            if (onTransferStatusChanged) {
                onTransferStatusChanged(id, TransferStatus::Active, "");
            }
            break;
        }
    }
}

void WasteCore::cancelTransfer(int id) {
    // Try to cancel real transfers first (without holding lock for network ops)
    if (!impl_->simulationMode && g_mql) {
        // Check downloads
        for (int x = 0; x < g_recvs.GetSize(); x++) {
            XferRecv* recv = g_recvs.Get(x);
            if (recv && (int)(intptr_t)recv == id) {
                recv->Abort(g_mql);
                delete recv;
                g_recvs.Del(x);
                if (onTransferStatusChanged) {
                    onTransferStatusChanged(id, TransferStatus::Failed, "Cancelled");
                }
                return;
            }
        }

        // Check uploads
        for (int x = 0; x < g_sends.GetSize(); x++) {
            XferSend* send = g_sends.Get(x);
            if (send && (int)(intptr_t)send == id) {
                send->Abort(g_mql);
                delete send;
                g_sends.Del(x);
                if (onTransferStatusChanged) {
                    onTransferStatusChanged(id, TransferStatus::Failed, "Cancelled");
                }
                return;
            }
        }
    }

    // Handle simulated transfers
    std::lock_guard<std::mutex> lock(mutex_);

    // Remove from simulated transfers
    impl_->simulatedTransfers.erase(
        std::remove_if(impl_->simulatedTransfers.begin(),
                       impl_->simulatedTransfers.end(),
                       [id](const SimulatedTransfer& st) { return st.id == id; }),
        impl_->simulatedTransfers.end());

    // Remove from transfers list
    impl_->transfers.erase(
        std::remove_if(impl_->transfers.begin(),
                       impl_->transfers.end(),
                       [id](const TransferInfo& t) { return t.id == id; }),
        impl_->transfers.end());

    if (onTransferStatusChanged) {
        onTransferStatusChanged(id, TransferStatus::Failed, "Cancelled");
    }
}

// Chat
void WasteCore::sendChatMessage(const std::string& room, const std::string& message) {
    debug_printf("[CHAT] sendChatMessage: room='%s' message='%s'\n",
        room.c_str(), message.c_str());

    // Echo the sent message back to local UI (outside any lock)
    ChatMessage msg;
    msg.room = room;
    msg.sender = nickname_.empty() ? "you" : nickname_;
    msg.content = message;
    msg.timestamp = std::chrono::system_clock::now();
    msg.isSystem = false;

    if (onChatMessage) {
        debug_printf("[CHAT] Echoing sent message to local UI\n");
        onChatMessage(msg);
    }

    if (!impl_->simulationMode && g_mql) {
        // Create and send real WASTE chat message
        C_MessageChat chatMsg;
        chatMsg.set_chatstring((char*)message.c_str());
        chatMsg.set_dest((char*)room.c_str());
        chatMsg.set_src(g_regnick[0] ? g_regnick : (char*)nickname_.c_str());

        T_Message wasteMsg = {0,};
        wasteMsg.data = chatMsg.Make();
        wasteMsg.message_type = MESSAGE_CHAT;
        if (wasteMsg.data) {
            wasteMsg.message_length = wasteMsg.data->GetLength();
            debug_printf("[CHAT] Sending MESSAGE_CHAT via g_mql (len=%d)\n",
                wasteMsg.message_length);
            g_mql->send(&wasteMsg);
        } else {
            debug_printf("[CHAT] ERROR: chatMsg.Make() returned NULL!\n");
        }
    } else if (impl_->simulationMode) {
        debug_printf("[CHAT] Simulation mode - not sending real message\n");
        // Simulate occasional response from a peer (30% chance)
        if ((rand() % 10) < 3) {
            static const char* responses[] = {
                "hi there!",
                "cool",
                "yeah",
                "nice",
                "lol",
                "sure thing",
                "got it",
                "interesting"
            };
            static const char* names[] = {"alice", "bob", "charlie", "dave"};

            ChatMessage reply;
            reply.room = room;
            reply.sender = names[rand() % 4];
            reply.content = responses[rand() % 8];
            reply.timestamp = std::chrono::system_clock::now();
            reply.isSystem = false;

            if (onChatMessage) {
                onChatMessage(reply);
            }
        }
    } else {
        debug_printf("[CHAT] Cannot send: not simulation and g_mql=%p\n", (void*)g_mql);
    }
}

void WasteCore::joinRoom(const std::string& room) {
    std::lock_guard<std::mutex> lock(mutex_);

    debug_printf("[CHAT] joinRoom: room='%s'\n", room.c_str());

    if (!impl_->simulationMode && g_mql) {
        // Send /join command - WASTE uses special format
        std::string joinCmd = "/join";
        C_MessageChat chatMsg;
        chatMsg.set_chatstring((char*)joinCmd.c_str());
        chatMsg.set_dest((char*)room.c_str());
        chatMsg.set_src(g_regnick[0] ? g_regnick : (char*)nickname_.c_str());

        T_Message wasteMsg = {0,};
        wasteMsg.data = chatMsg.Make();
        wasteMsg.message_type = MESSAGE_CHAT;
        if (wasteMsg.data) {
            wasteMsg.message_length = wasteMsg.data->GetLength();
            debug_printf("[CHAT] Sending /join to room '%s'\n", room.c_str());
            g_mql->send(&wasteMsg);
        }
    } else {
        debug_printf("[CHAT] joinRoom: not sending (sim=%d, g_mql=%p)\n",
            impl_->simulationMode, (void*)g_mql);
    }
}

void WasteCore::leaveRoom(const std::string& room) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!impl_->simulationMode && g_mql) {
        // Send /leave command
        std::string leaveCmd = "/leave";
        C_MessageChat chatMsg;
        chatMsg.set_chatstring((char*)leaveCmd.c_str());
        chatMsg.set_dest((char*)room.c_str());
        chatMsg.set_src(g_regnick[0] ? g_regnick : (char*)nickname_.c_str());

        T_Message wasteMsg = {0,};
        wasteMsg.data = chatMsg.Make();
        wasteMsg.message_type = MESSAGE_CHAT;
        if (wasteMsg.data) {
            wasteMsg.message_length = wasteMsg.data->GetLength();
            g_mql->send(&wasteMsg);
        }
    }
}

// Browse peer files
// WASTE uses search with "/" prefix for browsing
// Format: /nickname/path/* - the nickname is required
void WasteCore::browsePeer(const std::string& peer, const std::string& path) {
    debug_printf("[BROWSE] browsePeer: peer='%s' path='%s'\n", peer.c_str(), path.c_str());

    if (!g_mql || impl_->simulationMode) {
        debug_printf("[BROWSE] Cannot browse: no connection or simulation mode\n");
        return;
    }

    // Store browse state (both in impl_ and static for callback access)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        impl_->browsingPeer = peer;
        impl_->browsePath = path;
    }
    {
        std::lock_guard<std::mutex> lock(g_browse_mutex);
        g_browse_path = path;
    }

    // Build browse query - WASTE format is /nickname/path/*
    // The peer nickname MUST be included for the remote to respond
    std::string browseQuery = "/" + peer;
    if (path == "/" || path.empty()) {
        browseQuery += "/*";
    } else {
        browseQuery += path;
        if (browseQuery.back() != '/') {
            browseQuery += "/";
        }
        browseQuery += "*";
    }

    debug_printf("[BROWSE] Sending browse query: '%s'\n", browseQuery.c_str());

    // Send as a search request
    C_MessageSearchRequest req;
    req.set_min_conspeed(0);
    req.set_searchstring(const_cast<char*>(browseQuery.c_str()));

    T_Message msg = {0,};
    msg.data = req.Make();
    if (msg.data) {
        msg.message_type = MESSAGE_SEARCH;
        msg.message_length = msg.data->GetLength();
        g_mql->send(&msg);
        debug_printf("[BROWSE] Sent browse request\n");
    }
}

// Configuration
void WasteCore::setNickname(const std::string& nick) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string oldNick = nickname_;
    nickname_ = nick;

    // Update global WASTE nick
    strncpy(g_regnick, nick.c_str(), sizeof(g_regnick) - 1);
    g_regnick[sizeof(g_regnick) - 1] = '\0';

    // Broadcast nick change if connected
    if (!impl_->simulationMode && g_mql && !oldNick.empty() && oldNick != nick) {
        // Send /nick/oldnick to broadcast the change
        std::string nickCmd = "/nick/" + oldNick;
        C_MessageChat chatMsg;
        chatMsg.set_chatstring((char*)nickCmd.c_str());
        chatMsg.set_dest((char*)"&");  // Broadcast channel
        chatMsg.set_src(g_regnick);

        T_Message wasteMsg = {0,};
        wasteMsg.data = chatMsg.Make();
        wasteMsg.message_type = MESSAGE_CHAT;
        if (wasteMsg.data) {
            wasteMsg.message_length = wasteMsg.data->GetLength();
            g_mql->send(&wasteMsg);
        }
    }
}

std::string WasteCore::getNickname() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return nickname_;
}

void WasteCore::setListenPort(int port) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (port == listenPort_) return;
    listenPort_ = port;

    // Rebind listen socket if running
    if (!impl_->simulationMode && port > 0) {
        if (g_listen) {
            delete g_listen;
            g_listen = nullptr;
        }
        g_listen = new C_Listen((short)port);
        if (g_listen->is_error()) {
            debug_printf("[NET] Failed to rebind listen socket to port %d\n", port);
            delete g_listen;
            g_listen = nullptr;
        } else {
            debug_printf("[NET] Rebound listen socket to port %d\n", port);
        }
    }
}

int WasteCore::getListenPort() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return listenPort_;
}

void WasteCore::setNetworkName(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    networkName_ = name;

    // Recompute network hash (used by Blowfish for connection encryption)
    if (!name.empty()) {
        SHAify sha;
        sha.add((unsigned char*)name.c_str(), name.length());
        sha.final(g_networkhash);
        g_use_networkhash = 1;
        debug_printf("[NET] Updated network hash for '%s'\n", name.c_str());
    } else {
        memset(g_networkhash, 0, sizeof(g_networkhash));
        g_use_networkhash = 0;
        debug_printf("[NET] Cleared network hash (open network)\n");
    }
}

std::string WasteCore::getNetworkName() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return networkName_;
}

void WasteCore::setAcceptIncoming(bool accept) {
    std::lock_guard<std::mutex> lock(mutex_);
    // Bit 1 of g_accept_downloads controls responding to file requests
    if (accept) {
        g_accept_downloads |= 1;
    } else {
        g_accept_downloads &= ~1;
    }
    debug_printf("[NET] Accept incoming: %s (g_accept_downloads=%d)\n",
                 accept ? "on" : "off", g_accept_downloads);
}

bool WasteCore::getAcceptIncoming() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return (g_accept_downloads & 1) != 0;
}

void WasteCore::setThrottleUpload(bool enabled, int kbps) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (enabled) {
        g_throttle_flag |= 2;   // Bit 2 = throttle send
        g_throttle_send = kbps;
    } else {
        g_throttle_flag &= ~2;
        g_throttle_send = 0;
    }
    debug_printf("[NET] Upload throttle: %s (%d KB/s, flag=%d)\n",
                 enabled ? "on" : "off", kbps, g_throttle_flag);
}

void WasteCore::setThrottleDownload(bool enabled, int kbps) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (enabled) {
        g_throttle_flag |= 1;   // Bit 1 = throttle recv
        g_throttle_recv = kbps;
    } else {
        g_throttle_flag &= ~1;
        g_throttle_recv = 0;
    }
    debug_printf("[NET] Download throttle: %s (%d KB/s, flag=%d)\n",
                 enabled ? "on" : "off", kbps, g_throttle_flag);
}

bool WasteCore::getThrottleUploadEnabled() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return (g_throttle_flag & 2) != 0;
}

bool WasteCore::getThrottleDownloadEnabled() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return (g_throttle_flag & 1) != 0;
}

int WasteCore::getThrottleUploadKBps() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return g_throttle_send;
}

int WasteCore::getThrottleDownloadKBps() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return g_throttle_recv;
}

bool WasteCore::loadConfig(const std::string& configDir) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Use provided configDir, or fall back to stored one
    std::string dir = configDir.empty() ? configDir_ : configDir;
    if (!dir.empty() && configDir_.empty()) {
        configDir_ = dir;  // Store for later use
    }

    fs::path configPath = fs::path(dir) / "waste-tui.ini";

    if (!fs::exists(configPath)) {
        // No config file yet - generate default nickname
        char defaultNick[32];
        snprintf(defaultNick, sizeof(defaultNick), "user_%04x", (unsigned)(rand() & 0xFFFF));
        nickname_ = defaultNick;
        strncpy(g_regnick, defaultNick, sizeof(g_regnick) - 1);
        g_regnick[sizeof(g_regnick) - 1] = '\0';
        return false;
    }

    // Use C_Config to read the settings
    C_Config cfg((char*)configPath.c_str());

    // Read settings
    char* nick = cfg.ReadString((char*)"nickname", (char*)"");
    if (nick && nick[0]) {
        nickname_ = nick;
        strncpy(g_regnick, nick, sizeof(g_regnick) - 1);
        g_regnick[sizeof(g_regnick) - 1] = '\0';
    } else {
        // Generate a default nickname if none set
        char defaultNick[32];
        snprintf(defaultNick, sizeof(defaultNick), "user_%04x", (unsigned)(rand() & 0xFFFF));
        nickname_ = defaultNick;
        strncpy(g_regnick, defaultNick, sizeof(g_regnick) - 1);
        g_regnick[sizeof(g_regnick) - 1] = '\0';
    }

    listenPort_ = cfg.ReadInt((char*)"port", 4001);
    g_port = listenPort_;

    char* netName = cfg.ReadString((char*)"network", (char*)"");
    if (netName) {
        networkName_ = netName;
    }

    g_accept_downloads = cfg.ReadInt((char*)"downloadflags", 7);
    g_throttle_flag = cfg.ReadInt((char*)"throttleflag", 0);
    g_throttle_send = cfg.ReadInt((char*)"throttlesend", 128);
    g_throttle_recv = cfg.ReadInt((char*)"throttlerecv", 128);

    // Load shared directories (semicolon-separated)
    char* sharedDirsStr = cfg.ReadString((char*)"shared_dirs", (char*)"");
    if (sharedDirsStr && sharedDirsStr[0]) {
        impl_->sharedDirs.clear();
        std::string dirs = sharedDirsStr;
        size_t pos = 0;
        while ((pos = dirs.find(';')) != std::string::npos || !dirs.empty()) {
            std::string dir;
            if (pos != std::string::npos) {
                dir = dirs.substr(0, pos);
                dirs = dirs.substr(pos + 1);
            } else {
                dir = dirs;
                dirs.clear();
            }
            if (!dir.empty()) {
                impl_->sharedDirs.push_back(dir);
                debug_printf("[CONFIG] Loaded shared directory: '%s'\n", dir.c_str());
            }
        }
    }

    return true;
}

bool WasteCore::saveConfig() {
    std::lock_guard<std::mutex> lock(mutex_);

    // Ensure config directory exists
    fs::create_directories(configDir_);

    fs::path configPath = fs::path(configDir_) / "waste-tui.ini";

    // Use C_Config to write the settings
    C_Config cfg((char*)configPath.c_str());

    cfg.WriteString((char*)"nickname", (char*)nickname_.c_str());
    cfg.WriteInt((char*)"port", listenPort_);
    cfg.WriteString((char*)"network", (char*)networkName_.c_str());
    cfg.WriteInt((char*)"downloadflags", g_accept_downloads);
    cfg.WriteInt((char*)"throttleflag", g_throttle_flag);
    cfg.WriteInt((char*)"throttlesend", g_throttle_send);
    cfg.WriteInt((char*)"throttlerecv", g_throttle_recv);

    // Save shared directories (semicolon-separated)
    std::string sharedDirsStr;
    for (size_t i = 0; i < impl_->sharedDirs.size(); i++) {
        if (i > 0) sharedDirsStr += ";";
        sharedDirsStr += impl_->sharedDirs[i];
    }
    cfg.WriteString((char*)"shared_dirs", (char*)sharedDirsStr.c_str());

    cfg.Flush();

    return true;
}

}  // namespace waste
