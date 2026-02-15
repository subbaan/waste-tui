#include "app.h"
#include "theme.h"
#include "components/table.h"
#include "components/scrolltext.h"
#include "components/modal.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/component/loop.hpp>

#include <chrono>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <filesystem>

namespace waste {

using namespace ftxui;

namespace {

constexpr char CTRL_A = '\x01';
constexpr char CTRL_B = '\x02';
constexpr char CTRL_D = '\x04';
constexpr char CTRL_E = '\x05';
constexpr char CTRL_F = '\x06';
constexpr char CTRL_L = '\x0C';
constexpr char CTRL_N = '\x0E';
constexpr char CTRL_O = '\x0F';
constexpr char CTRL_P = '\x10';
constexpr char CTRL_Q = '\x11';
constexpr char CTRL_R = '\x12';
constexpr char CTRL_S = '\x13';
constexpr char CTRL_T = '\x14';
constexpr char CTRL_X = '\x18';

std::string formatTime() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&time);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%H:%M:%S");
    return oss.str();
}

std::string formatSpeed(float kbps) {
    std::ostringstream oss;
    if (kbps >= 1024) {
        oss << std::fixed << std::setprecision(1) << (kbps / 1024) << " MB/s";
    } else {
        oss << std::fixed << std::setprecision(1) << kbps << " KB/s";
    }
    return oss.str();
}

std::string formatDuration(std::chrono::steady_clock::time_point start) {
    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::minutes>(now - start).count();
    if (duration < 60) {
        return std::to_string(duration) + "m";
    }
    int hours = duration / 60;
    int mins = duration % 60;
    return std::to_string(hours) + "h " + std::to_string(mins) + "m";
}

std::string connectionStatusStr(ConnectionStatus status) {
    switch (status) {
        case ConnectionStatus::Connecting: return "○ Conn..";
        case ConnectionStatus::Authenticating: return "◐ Auth..";
        case ConnectionStatus::Online: return "● Online";
        case ConnectionStatus::Failed: return "✗ Failed";
    }
    return "?";
}

std::string formatSize(uint64_t bytes) {
    std::ostringstream oss;
    if (bytes >= 1024ULL * 1024 * 1024) {
        oss << std::fixed << std::setprecision(1) << (bytes / (1024.0 * 1024 * 1024)) << " GB";
    } else if (bytes >= 1024 * 1024) {
        oss << std::fixed << std::setprecision(1) << (bytes / (1024.0 * 1024)) << " MB";
    } else if (bytes >= 1024) {
        oss << std::fixed << std::setprecision(1) << (bytes / 1024.0) << " KB";
    } else {
        oss << bytes << " B";
    }
    return oss.str();
}

std::string transferStatusStr(TransferStatus status) {
    switch (status) {
        case TransferStatus::Active: return "Active";
        case TransferStatus::Paused: return "Paused";
        case TransferStatus::Queued: return "Queued";
        case TransferStatus::Completed: return "Done";
        case TransferStatus::Failed: return "Failed";
    }
    return "?";
}

// Render message content with clickable URLs
// Detects http://, https://, ftp://, and www. prefixes
Element renderMessageContent(const std::string& content, Color linkColor = Color::Blue) {
    Elements parts;
    size_t pos = 0;
    size_t len = content.length();

    while (pos < len) {
        // Find the next URL
        size_t urlStart = std::string::npos;
        std::string urlPrefix;

        static const std::vector<std::string> prefixes = {
            "https://", "http://", "ftp://", "www."
        };

        for (const auto& prefix : prefixes) {
            size_t found = content.find(prefix, pos);
            if (found < urlStart) {
                urlStart = found;
                urlPrefix = prefix;
            }
        }

        if (urlStart == std::string::npos) {
            // No more URLs, emit remaining text
            parts.push_back(text(content.substr(pos)));
            break;
        }

        // Emit text before the URL
        if (urlStart > pos) {
            parts.push_back(text(content.substr(pos, urlStart - pos)));
        }

        // Find end of URL (whitespace or certain punctuation)
        size_t urlEnd = urlStart + urlPrefix.length();
        while (urlEnd < len && content[urlEnd] != ' ' && content[urlEnd] != '\t' &&
               content[urlEnd] != '>' && content[urlEnd] != '"' && content[urlEnd] != '\n') {
            urlEnd++;
        }
        // Strip trailing punctuation that's likely not part of the URL
        while (urlEnd > urlStart + urlPrefix.length() &&
               (content[urlEnd - 1] == '.' || content[urlEnd - 1] == ',' ||
                content[urlEnd - 1] == ')' || content[urlEnd - 1] == ']')) {
            urlEnd--;
        }

        std::string url = content.substr(urlStart, urlEnd - urlStart);
        std::string linkUrl = url;
        // Auto-prefix www. with https://
        if (url.substr(0, 4) == "www.") {
            linkUrl = "https://" + url;
        }

        parts.push_back(
            text(url) | underlined | color(linkColor) | hyperlink(linkUrl)
        );

        pos = urlEnd;
    }

    if (parts.empty()) {
        return text("");
    }
    if (parts.size() == 1) {
        return parts[0];
    }
    return hbox(parts);
}

}  // namespace

App::App() : screen_(ScreenInteractive::Fullscreen()) {
    modalPortInput_ = "4001";

    // Set up config directory
    const char* home = std::getenv("HOME");
    if (home) {
        configDir_ = std::string(home) + "/.waste";
    } else {
        configDir_ = ".waste";
    }

    // Create core instance
    core_ = std::make_unique<WasteCore>();
}

App::~App() {
    running_ = false;
    if (core_) {
        core_->shutdown();
    }
}

bool App::initializeCore() {
    // Check if keys exist
    if (!core_->keysExist(configDir_)) {
        needsFirstRun_ = true;
        return false;
    }

    // Load config file if it exists
    core_->loadConfig(configDir_);

    // Sync config to state
    {
        std::lock_guard<std::mutex> lock(state_.mutex());
        state_.setListenPort(core_->getListenPort());
        std::string nick = core_->getNickname();
        if (!nick.empty()) {
            state_.setNickname(nick);
        }
        std::string netName = core_->getNetworkName();
        if (!netName.empty()) {
            state_.setNetworkHash(netName);
        }
        state_.setAcceptIncoming(core_->getAcceptIncoming());
        state_.setLimitUpload(core_->getThrottleUploadEnabled());
        state_.setLimitDownload(core_->getThrottleDownloadEnabled());
        state_.setUploadLimitKBps(core_->getThrottleUploadKBps() > 0 ? core_->getThrottleUploadKBps() : 128);
        state_.setDownloadLimitKBps(core_->getThrottleDownloadKBps() > 0 ? core_->getThrottleDownloadKBps() : 128);

        // Sync shared directories
        auto sharedDirs = core_->getSharedDirectories();
        state_.sharedDirs().clear();
        for (const auto& dir : sharedDirs) {
            SharedDirectory sd;
            sd.path = dir;
            sd.fileCount = 0;
            sd.totalSize = 0;
            sd.scanning = true;
            state_.sharedDirs().push_back(sd);
        }

        // Sync theme
        state_.setThemeIndex(findThemeIndex(core_->getThemeName()));
    }

    // Set up callbacks before initializing
    setupCoreCallbacks();

    // Initialize the core with loaded port
    auto result = core_->initialize(configDir_, core_->getListenPort());

    if (result == WasteCore::InitResult::NoKeys) {
        needsFirstRun_ = true;
        return false;
    }

    if (result == WasteCore::InitResult::Success) {
        // Trigger rescan if we have shared directories
        auto sharedDirs = core_->getSharedDirectories();
        if (!sharedDirs.empty()) {
            core_->rescanSharedDirectories();
        }
        return true;
    }

    return false;
}

bool App::generateNewKeys() {
    if (core_->generateKeys(configDir_)) {
        needsFirstRun_ = false;
        return initializeCore();
    }
    return false;
}

bool App::importExistingKeys(const std::string& path) {
    if (core_->importKeys(path, configDir_)) {
        needsFirstRun_ = false;
        return initializeCore();
    }
    return false;
}

void App::startSimulation() {
    // Set up callbacks for simulation mode
    setupCoreCallbacks();

    // Initialize core in simulation-only mode (doesn't require keys)
    core_->initialize(configDir_, 0);  // port 0 = no listen
}

void App::setupCoreCallbacks() {
    // Peer connected
    core_->onPeerConnected = [this](const PeerInfo& peer) {
        post([this, peer] {
            std::lock_guard<std::mutex> lock(state_.mutex());
            state_.peers().push_back(peer);
        });
        refresh();
    };

    // Peer status changed - uses index into simulatedPeers, need to find by address
    core_->onPeerStatusChanged = [this](int simIndex, ConnectionStatus status, const std::string& error) {
        // Get address from core's simulated peer to find in state
        // For now, search from the end since new peers are appended
        post([this, simIndex, status, error] {
            std::lock_guard<std::mutex> lock(state_.mutex());
            auto& peers = state_.peers();
            // Find the peer added most recently with Connecting/Authenticating status
            // and update it - this is a workaround for index mismatch with demo data
            for (int i = peers.size() - 1; i >= 0; i--) {
                if (peers[i].status == ConnectionStatus::Connecting ||
                    peers[i].status == ConnectionStatus::Authenticating) {
                    peers[i].status = status;
                    peers[i].errorMsg = error;
                    if (status == ConnectionStatus::Online) {
                        peers[i].connectedAt = std::chrono::steady_clock::now();
                    }
                    break;
                }
            }
        });
        refresh();
    };

    // Peer disconnected
    core_->onPeerDisconnected = [this](int index) {
        post([this, index] {
            std::lock_guard<std::mutex> lock(state_.mutex());
            auto& peers = state_.peers();
            if (index >= 0 && index < (int)peers.size()) {
                peers.erase(peers.begin() + index);
            }
        });
        refresh();
    };

    // Search result
    core_->onSearchResult = [this](const SearchResult& result) {
        post([this, result] {
            std::lock_guard<std::mutex> lock(state_.mutex());
            state_.searchResults().push_back(result);
        });
        refresh();
    };

    // Browse results (peer file listing)
    core_->onBrowseResults = [this](const std::string& peerGuid, const std::vector<BrowseEntry>& entries) {
        post([this, peerGuid, entries] {
            std::lock_guard<std::mutex> lock(state_.mutex());
            state_.setBrowsePeerGuid(peerGuid);
            state_.browseEntries().clear();
            for (const auto& entry : entries) {
                state_.browseEntries().push_back(entry);
            }
        });
        refresh();
    };

    // Transfer added
    core_->onTransferAdded = [this](const TransferInfo& transfer) {
        post([this, transfer] {
            std::lock_guard<std::mutex> lock(state_.mutex());
            state_.transfers().push_back(transfer);
        });
        refresh();
    };

    // Transfer progress
    core_->onTransferProgress = [this](int id, uint64_t transferred, uint64_t totalSize, float speedKBps) {
        post([this, id, transferred, totalSize, speedKBps] {
            std::lock_guard<std::mutex> lock(state_.mutex());
            for (auto& t : state_.transfers()) {
                if (t.id == id) {
                    t.transferred = transferred;
                    // Update totalSize if we now know it (was 0 when download started)
                    if (totalSize > 0) {
                        t.totalSize = totalSize;
                    }
                    t.speedKBps = speedKBps;
                    break;
                }
            }
        });
        refresh();
    };

    // Chat message
    core_->onChatMessage = [this](const ChatMessage& msg) {
        post([this, msg] {
            std::lock_guard<std::mutex> lock(state_.mutex());
            // Find or create room
            bool found = false;
            for (auto& room : state_.chatRooms()) {
                if (room.name == msg.room) {
                    room.messages.push_back(msg);
                    if (state_.selectedRoomIndex() != (int)(&room - &state_.chatRooms()[0])) {
                        room.unreadCount++;
                    }
                    found = true;
                    break;
                }
            }
            if (!found) {
                // Create new room
                ChatRoom newRoom;
                newRoom.name = msg.room;
                newRoom.isDirect = (msg.room[0] != '#');
                newRoom.messages.push_back(msg);
                newRoom.unreadCount = 1;
                state_.chatRooms().push_back(newRoom);
            }
        });
        refresh();
    };

    // User presence (join/leave/nick change)
    core_->onUserPresence = [this](const std::string& room, const std::string& user, bool joined) {
        post([this, room, user, joined] {
            std::lock_guard<std::mutex> lock(state_.mutex());
            // Find the room
            for (auto& r : state_.chatRooms()) {
                if (r.name == room) {
                    if (joined) {
                        // Add user if not already present
                        auto it = std::find(r.users.begin(), r.users.end(), user);
                        if (it == r.users.end()) {
                            r.users.push_back(user);
                            std::sort(r.users.begin(), r.users.end());
                        }
                    } else {
                        // Remove user
                        r.users.erase(
                            std::remove(r.users.begin(), r.users.end(), user),
                            r.users.end());
                    }
                    break;
                }
            }
        });
        refresh();
    };

    // Peer nickname learned from protocol (ping, chat reply, etc.)
    core_->onPeerNicknameChanged = [this](const std::string& address, const std::string& nickname) {
        post([this, address, nickname] {
            std::lock_guard<std::mutex> lock(state_.mutex());
            for (auto& peer : state_.peers()) {
                if (peer.address == address) {
                    peer.nickname = nickname;
                    break;
                }
            }
        });
        refresh();
    };

    // Network stats
    core_->onNetworkStatsUpdated = [this](const NetworkStats& stats) {
        post([this, stats] {
            std::lock_guard<std::mutex> lock(state_.mutex());
            state_.setNetworkStats(stats);
        });
        refresh();
    };
}

void App::run() {
    auto ui = buildUI();

    // Use standard FTXUI loop - it handles rendering and events properly
    screen_.Loop(ui);
}

void App::refresh() {
    screen_.PostEvent(Event::Custom);
}

void App::post(std::function<void()> callback) {
    screen_.Post(callback);
}

void App::quit() {
    running_ = false;
    screen_.Exit();
}

void App::refreshBrowserEntries() {
    namespace fs = std::filesystem;
    browserEntries_.clear();

    // Add parent directory option if not at root
    if (browserCurrentPath_ != "/") {
        browserEntries_.push_back("..");
    }

    // List directories (for folder selection)
    std::vector<std::string> dirs;
    std::vector<std::string> hiddenDirs;

    try {
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(
                browserCurrentPath_,
                fs::directory_options::skip_permission_denied, ec)) {
            // Use error_code overload to avoid throwing on individual entries
            std::error_code entryEc;
            bool isDir = entry.is_directory(entryEc);
            // Also check symlinks pointing to directories
            if (!isDir && !entryEc) {
                isDir = entry.is_symlink(entryEc) &&
                        fs::is_directory(entry.path(), entryEc);
            }
            if (!isDir) continue;

            std::string name = entry.path().filename().string();
            if (name.empty()) continue;

            if (name[0] == '.') {
                hiddenDirs.push_back(name);
            } else {
                dirs.push_back(name);
            }
        }
    } catch (...) {
        // directory_iterator constructor failed - leave dirs empty
    }

    // Sort and add: regular dirs first, then hidden dirs
    std::sort(dirs.begin(), dirs.end());
    std::sort(hiddenDirs.begin(), hiddenDirs.end());
    for (const auto& d : dirs) {
        browserEntries_.push_back(d);
    }
    for (const auto& d : hiddenDirs) {
        browserEntries_.push_back(d);
    }
}

Element App::buildStatusBar() {
    std::lock_guard<std::mutex> lock(state_.mutex());
    auto stats = state_.networkStats();

    // Show unread chat indicator
    int totalUnread = 0;
    for (const auto& room : state_.chatRooms()) {
        totalUnread += room.unreadCount;
    }

    const auto& t = state_.theme();

    Elements statusItems = {
        text(" WASTE v1.10.1 ") | bold | color(t.accent),
        separator(),
        text(" Net: " + std::to_string(stats.connectedPeers) + " peers "),
        separator(),
        text(" ↑ " + formatSpeed(stats.uploadKBps) + " ") | color(t.success),
        separator(),
        text(" ↓ " + formatSpeed(stats.downloadKBps) + " ") | color(t.warning),
    };

    if (totalUnread > 0) {
        statusItems.push_back(separator());
        statusItems.push_back(text(" ✉ " + std::to_string(totalUnread) + " ") | color(t.notification) | bold);
    }

    statusItems.push_back(separator());
    statusItems.push_back(filler());
    statusItems.push_back(text(" " + formatTime() + " "));

    return hbox(statusItems) | bgcolor(t.bgDark);
}

Element App::buildTabBar() {
    std::lock_guard<std::mutex> lock(state_.mutex());
    const auto& t = state_.theme();

    std::vector<std::string> tabNames = {
        "F1 Network", "F2 Search", "F3 Transfers", "F4 Chat", "F5 Keys", "F6 Settings"
    };

    Elements tabs;
    for (size_t i = 0; i < tabNames.size(); ++i) {
        auto tabText = text(" " + tabNames[i] + " ");
        if ((int)i == tabIndex_) {
            tabText = tabText | bold | bgcolor(t.primary) | color(t.primaryFg);
        } else {
            tabText = tabText | dim;
        }
        tabs.push_back(tabText);
    }

    return hbox(tabs);
}

Element App::buildFooter() {
    std::string hints;
    std::lock_guard<std::mutex> lock(state_.mutex());

    if (state_.showModal()) {
        hints = "Enter:Confirm  Esc:Cancel  Tab:Next field";
    } else {
        switch (state_.currentView()) {
            case View::Network:
                hints = "^A:Add  ^D:Disconnect  ^B:Browse  ^N:Chat  ^R:Retry  ?:Help";
                break;
            case View::Search:
                hints = "/:Search  Esc:Clear  ^D:Download  ^B:Browse user  ?:Help";
                break;
            case View::Transfers:
                hints = "^P:Pause  ^R:Resume  ^D:Cancel  ^X:Clear done  ?:Help";
                break;
            case View::Chat:
                hints = "Tab:Rooms  Enter:Send  ^O:Join  ^N:DM  ^L:Leave  F7:Help";
                break;
            case View::Browse:
                hints = "Enter/l:Open  h/Bksp:Up  ^D:Download  Esc:Back  ?:Help";
                break;
            case View::Keys:
                hints = "Tab:Lists  ^T:Trust  ^D:Delete  ^F:Import  ^E:Export  ?:Help";
                break;
            case View::Settings:
                hints = "arrows:Nav  Space:Toggle  ^A:Add  ^D:Delete  ^S:Save  ?:Help";
                break;
        }
    }

    const auto& t = state_.theme();
    return hbox({
        text(" " + hints + " ") | dim,
        filler(),
        text(" ^Q:Quit ") | dim
    }) | bgcolor(t.bgDark);
}

Element App::buildHelpOverlay() {
    std::lock_guard<std::mutex> lock(state_.mutex());

    std::string title;
    std::vector<std::pair<std::string, std::string>> bindings;

    // Global bindings
    bindings.push_back({"F1-F6", "Switch views"});
    bindings.push_back({"Ctrl+Q/F10", "Quit"});
    bindings.push_back({"Esc", "Close/Back"});
    bindings.push_back({"?/F7", "Toggle help"});
    bindings.push_back({"", ""});

    switch (state_.currentView()) {
        case View::Network:
            title = "Network View Help";
            bindings.push_back({"j/k or ↑/↓", "Navigate peers"});
            bindings.push_back({"Ctrl+A", "Add connection"});
            bindings.push_back({"Ctrl+D", "Disconnect peer"});
            bindings.push_back({"Ctrl+B", "Browse peer's files"});
            bindings.push_back({"Ctrl+N", "Chat with peer"});
            bindings.push_back({"Ctrl+R", "Retry failed connection"});
            break;
        case View::Search:
            title = "Search View Help";
            bindings.push_back({"Enter", "Execute search"});
            bindings.push_back({"Escape", "Cancel search / clear results"});
            bindings.push_back({"/ or Ctrl+N", "Focus search input"});
            bindings.push_back({"j/k or ↑/↓", "Navigate results"});
            bindings.push_back({"Ctrl+D", "Download selected"});
            bindings.push_back({"Ctrl+B", "Browse file owner"});
            break;
        case View::Transfers:
            title = "Transfers View Help";
            bindings.push_back({"j/k or ↑/↓", "Navigate transfers"});
            bindings.push_back({"Tab", "Switch Download/Upload"});
            bindings.push_back({"Ctrl+P", "Pause transfer"});
            bindings.push_back({"Ctrl+R", "Resume transfer"});
            bindings.push_back({"Ctrl+D", "Cancel transfer"});
            bindings.push_back({"Ctrl+X", "Clear completed"});
            break;
        case View::Chat:
            title = "Chat View Help";
            bindings.push_back({"Tab", "Switch room list/input"});
            bindings.push_back({"↑/↓", "Navigate rooms"});
            bindings.push_back({"Enter", "Send message"});
            bindings.push_back({"PgUp/PgDn", "Scroll messages"});
            bindings.push_back({"Ctrl+O", "Join room"});
            bindings.push_back({"Ctrl+N", "Direct message"});
            bindings.push_back({"Ctrl+L", "Leave room"});
            break;
        case View::Browse:
            title = "Browse View Help";
            bindings.push_back({"j/k or ↑/↓", "Navigate files"});
            bindings.push_back({"Enter or l", "Open folder / Download"});
            bindings.push_back({"Backspace/h", "Go to parent"});
            bindings.push_back({"Ctrl+D", "Download selected"});
            bindings.push_back({"Esc", "Back to previous"});
            break;
        case View::Keys:
            title = "Keys View Help";
            bindings.push_back({"j/k or ↑/↓", "Navigate keys"});
            bindings.push_back({"Tab", "Switch trusted/pending"});
            bindings.push_back({"Ctrl+T", "Trust pending key"});
            bindings.push_back({"Ctrl+D", "Delete selected key"});
            bindings.push_back({"Ctrl+F", "Import key file"});
            bindings.push_back({"Ctrl+E", "Export public key"});
            break;
        case View::Settings:
            title = "Settings View Help";
            bindings.push_back({"↑/↓", "Navigate sections"});
            bindings.push_back({"Tab", "Move between fields"});
            bindings.push_back({"Space/Enter", "Toggle checkbox"});
            bindings.push_back({"Ctrl+A", "Add (directory/etc)"});
            bindings.push_back({"Ctrl+D", "Delete selected"});
            bindings.push_back({"Ctrl+S", "Save settings"});
            break;
    }

    Elements lines;
    for (const auto& [key, desc] : bindings) {
        if (key.empty()) {
            lines.push_back(separator());
        } else {
            lines.push_back(hbox({
                text(key) | bold | size(WIDTH, EQUAL, 14),
                text(desc)
            }));
        }
    }

    return window(
        text(" " + title + " ") | bold,
        vbox(lines) | size(WIDTH, EQUAL, 40)
    ) | clear_under | center;
}

bool App::handleGlobalEvent(Event event) {
    // Handle modal input first if a modal is showing
    {
        std::lock_guard<std::mutex> lock(state_.mutex());
        if (state_.showModal()) {
            const auto& type = state_.modalType();

            // Escape always closes modal
            if (event == Event::Escape) {
                state_.setShowModal(false);
                modalAddressInput_.clear();
                modalPortInput_.clear();
                modalRoomInput_.clear();
                modalPathInput_.clear();
                modalFieldFocus_ = 0;
                return true;
            }

            // Handle confirm modals (yes/no)
            if (type == "confirm_disconnect" || type == "download_confirm") {
                if (event == Event::Character('y') || event == Event::Character('Y')) {
                    if (type == "confirm_disconnect") {
                        int idx = state_.selectedPeerIndex();
                        auto& peers = state_.peers();
                        if (idx >= 0 && idx < (int)peers.size()) {
                            peers.erase(peers.begin() + idx);
                            if (idx > 0 && idx >= (int)peers.size()) {
                                state_.setSelectedPeerIndex(idx - 1);
                            }
                        }
                    } else if (type == "download_confirm") {
                        // Extract download info and start in a background post
                        // to avoid blocking UI (XferRecv does network I/O)
                        int idx = state_.selectedSearchIndex();
                        auto& results = state_.searchResults();
                        if (idx >= 0 && idx < (int)results.size()) {
                            std::string hash = results[idx].hash;
                            std::string filename = results[idx].filename;
                            post([this, hash, filename] {
                                if (core_) {
                                    core_->downloadFile(hash, filename);
                                }
                            });
                        }
                    }
                    state_.setShowModal(false);
                    return true;
                }
                if (event == Event::Character('n') || event == Event::Character('N')) {
                    state_.setShowModal(false);
                    return true;
                }
                return true; // Consume all other events while confirm modal is open
            }

            // Handle input modals (single field)
            if (type == "join_room" || type == "add_directory" || type == "start_dm") {
                std::string* inputField = (type == "join_room") ? &modalRoomInput_
                    : (type == "start_dm") ? &modalDmPeerInput_ : &modalPathInput_;

                if (event == Event::Return) {
                    if (type == "join_room" && !modalRoomInput_.empty()) {
                        ChatRoom room;
                        room.name = modalRoomInput_[0] == '#' ? modalRoomInput_ : "#" + modalRoomInput_;
                        room.isDirect = false;
                        room.unreadCount = 0;
                        state_.chatRooms().push_back(room);
                        modalRoomInput_.clear();
                    } else if (type == "start_dm" && !modalDmPeerInput_.empty()) {
                        std::string peerNick = modalDmPeerInput_;
                        std::string dmRoom = "@" + peerNick;

                        // Check if DM room already exists
                        bool found = false;
                        int foundIdx = -1;
                        for (int i = 0; i < (int)state_.chatRooms().size(); i++) {
                            if (state_.chatRooms()[i].name == dmRoom) {
                                found = true;
                                foundIdx = i;
                                break;
                            }
                        }

                        if (!found) {
                            ChatRoom room;
                            room.name = dmRoom;
                            room.isDirect = true;
                            room.unreadCount = 0;
                            state_.chatRooms().push_back(room);
                            foundIdx = state_.chatRooms().size() - 1;
                        }

                        // Select the DM room
                        state_.setSelectedRoomIndex(foundIdx);
                        modalDmPeerInput_.clear();
                    } else if (type == "add_directory" && !modalPathInput_.empty()) {
                        SharedDirectory dir;
                        dir.path = modalPathInput_;
                        dir.fileCount = 0;
                        dir.totalSize = 0;
                        dir.scanning = true;
                        state_.sharedDirs().push_back(dir);
                        // Tell core to add this directory
                        if (core_) {
                            core_->addSharedDirectory(modalPathInput_);
                        }
                        modalPathInput_.clear();
                    }
                    state_.setShowModal(false);
                    return true;
                }
                if (event == Event::Backspace && !inputField->empty()) {
                    inputField->pop_back();
                    return true;
                }
                if (event.is_character()) {
                    *inputField += event.character();
                    return true;
                }
                return true; // Consume all events
            }

            // Handle folder browser modal
            if (type == "browse_directory") {
                namespace fs = std::filesystem;

                if (event == Event::Return) {
                    if (browserSelectedIndex_ == 0 && browserCurrentPath_ != "/" &&
                        !browserEntries_.empty() && browserEntries_[0] == "..") {
                        // Go to parent directory
                        fs::path p(browserCurrentPath_);
                        browserCurrentPath_ = p.parent_path().string();
                        if (browserCurrentPath_.empty()) browserCurrentPath_ = "/";
                        browserSelectedIndex_ = 0;
                        refreshBrowserEntries();
                    } else if (browserSelectedIndex_ < (int)browserEntries_.size()) {
                        std::string selected = browserEntries_[browserSelectedIndex_];
                        if (selected != "..") {
                            // Enter subdirectory
                            fs::path newPath = fs::path(browserCurrentPath_) / selected;
                            browserCurrentPath_ = newPath.string();
                            browserSelectedIndex_ = 0;
                            refreshBrowserEntries();
                        }
                    }
                    return true;
                }
                if (event == Event::Character('s') || event == Event::Character(' ')) {
                    // Select current directory
                    SharedDirectory dir;
                    dir.path = browserCurrentPath_;
                    dir.fileCount = 0;
                    dir.totalSize = 0;
                    dir.scanning = true;
                    state_.sharedDirs().push_back(dir);
                    if (core_) {
                        core_->addSharedDirectory(browserCurrentPath_);
                    }
                    state_.setShowModal(false);
                    return true;
                }
                if (event == Event::ArrowUp || event == Event::Character('k')) {
                    if (browserSelectedIndex_ > 0) {
                        browserSelectedIndex_--;
                    }
                    return true;
                }
                if (event == Event::ArrowDown || event == Event::Character('j')) {
                    if (browserSelectedIndex_ < (int)browserEntries_.size() - 1) {
                        browserSelectedIndex_++;
                    }
                    return true;
                }
                if (event == Event::Backspace || event == Event::Character('h')) {
                    // Go to parent
                    if (browserCurrentPath_ != "/") {
                        fs::path p(browserCurrentPath_);
                        browserCurrentPath_ = p.parent_path().string();
                        if (browserCurrentPath_.empty()) browserCurrentPath_ = "/";
                        browserSelectedIndex_ = 0;
                        refreshBrowserEntries();
                    }
                    return true;
                }
                return true; // Consume all events
            }

            // Handle key export modal
            if (type == "export_key") {
                if (event == Event::Return) {
                    if (!modalPathInput_.empty() && core_) {
                        if (core_->exportPublicKey(modalPathInput_)) {
                            // Success - could show a message
                        }
                    }
                    modalPathInput_.clear();
                    state_.setShowModal(false);
                    return true;
                }
                if (event == Event::Backspace && !modalPathInput_.empty()) {
                    modalPathInput_.pop_back();
                    return true;
                }
                if (event.is_character()) {
                    modalPathInput_ += event.character();
                    return true;
                }
                return true;
            }

            // Handle key import modal
            if (type == "import_key") {
                if (event == Event::Return) {
                    if (!modalPathInput_.empty() && core_) {
                        if (core_->importPublicKey(modalPathInput_)) {
                            // Success - key added to trusted list
                        }
                    }
                    modalPathInput_.clear();
                    state_.setShowModal(false);
                    return true;
                }
                if (event == Event::Backspace && !modalPathInput_.empty()) {
                    modalPathInput_.pop_back();
                    return true;
                }
                if (event.is_character()) {
                    modalPathInput_ += event.character();
                    return true;
                }
                return true;
            }

            // Handle two-field modal (add_connection)
            if (type == "add_connection") {
                std::string* currentField = (modalFieldFocus_ == 0) ? &modalAddressInput_ : &modalPortInput_;

                if (event == Event::Tab) {
                    modalFieldFocus_ = 1 - modalFieldFocus_; // Toggle 0/1
                    return true;
                }
                if (event == Event::Return) {
                    std::string addr = modalAddressInput_;
                    int port = 4001;
                    try {
                        port = std::stoi(modalPortInput_.empty() ? "4001" : modalPortInput_);
                    } catch (...) {
                        port = 4001;
                    }

                    // Clear modal state
                    modalAddressInput_.clear();
                    modalPortInput_.clear();
                    modalFieldFocus_ = 0;
                    state_.setShowModal(false);

                    if (!addr.empty()) {
                        // Call core - it will handle simulation and callback
                        if (core_ && core_->isRunning()) {
                            core_->connectToPeer(addr, port);
                        } else {
                            // Fallback: add demo peer directly if core not running
                            PeerInfo peer;
                            peer.address = addr;
                            peer.port = port;
                            peer.status = ConnectionStatus::Connecting;
                            peer.filesShared = 0;
                            peer.connectedAt = std::chrono::steady_clock::now();
                            state_.peers().push_back(peer);
                        }
                    }
                    return true;
                }
                if (event == Event::Backspace && !currentField->empty()) {
                    currentField->pop_back();
                    return true;
                }
                if (event.is_character()) {
                    *currentField += event.character();
                    return true;
                }
                return true; // Consume all events
            }

            return true; // Consume events while modal is open
        }
    }

    // Chat view: let per-view handler manage all single-character keys
    // This prevents typing conflicts — per-view handler uses chatInput().empty()
    // to distinguish navigation mode vs typing mode
    if (event.is_character()) {
        std::lock_guard<std::mutex> lock(state_.mutex());
        if (state_.currentView() == View::Chat) {
            return false;
        }
    }

    // Help toggle
    if (event == Event::Character('?') || event == Event::F7) {
        showHelp_ = !showHelp_;
        return true;
    }

    // Close help on any key if showing
    if (showHelp_ && event != Event::Custom) {
        showHelp_ = false;
        return true;
    }

    // F1-F5 for tab switching
    if (event == Event::F1) {
        tabIndex_ = 0;
        std::lock_guard<std::mutex> lock(state_.mutex());
        state_.setCurrentView(View::Network);
        return true;
    }
    if (event == Event::F2) {
        tabIndex_ = 1;
        std::lock_guard<std::mutex> lock(state_.mutex());
        state_.setCurrentView(View::Search);
        return true;
    }
    if (event == Event::F3) {
        tabIndex_ = 2;
        std::lock_guard<std::mutex> lock(state_.mutex());
        state_.setCurrentView(View::Transfers);
        return true;
    }
    if (event == Event::F4) {
        tabIndex_ = 3;
        std::lock_guard<std::mutex> lock(state_.mutex());
        state_.setCurrentView(View::Chat);
        return true;
    }
    if (event == Event::F5) {
        tabIndex_ = 4;
        std::lock_guard<std::mutex> lock(state_.mutex());
        state_.setCurrentView(View::Keys);
        return true;
    }
    if (event == Event::F6) {
        tabIndex_ = 5;
        std::lock_guard<std::mutex> lock(state_.mutex());
        state_.setCurrentView(View::Settings);
        return true;
    }

    // F10 or Ctrl+Q to quit
    if (event == Event::F10 ||
        event == Event::Character(CTRL_Q)) {
        quit();
        return true;
    }

    // Escape to go back from browse view
    if (event == Event::Escape) {
        std::lock_guard<std::mutex> lock(state_.mutex());
        if (state_.currentView() == View::Browse) {
            state_.setCurrentView(state_.previousView());
            tabIndex_ = static_cast<int>(state_.previousView());
            return true;
        }
    }

    // Handle Ctrl+A for adding based on current view
    if (event == Event::Character(CTRL_A)) {
        std::lock_guard<std::mutex> lock(state_.mutex());
        View view = state_.currentView();
        if (view == View::Network) {
            state_.setModalType("add_connection");
            state_.setShowModal(true);
            return true;
        } else if (view == View::Settings &&
                   state_.settingsSection() == SettingsSection::Sharing) {
            // Initialize folder browser
            browserCurrentPath_ = getenv("HOME") ? getenv("HOME") : "/";
            browserSelectedIndex_ = 0;
            refreshBrowserEntries();
            state_.setModalType("browse_directory");
            state_.setShowModal(true);
            return true;
        }
    }

    // Handle 'j' / ArrowDown for navigation
    // Skip 'j'/'k' in Chat view - let the chat input handle them for typing
    if (event == Event::Character('j') || event == Event::ArrowDown) {
        std::lock_guard<std::mutex> lock(state_.mutex());
        View view = state_.currentView();

        // Settings navigation down
        if (view == View::Settings) {
            if (!settingsFocusContent_) {
                // Navigate sections
                int section = static_cast<int>(state_.settingsSection());
                if (section < 3) {
                    state_.setSettingsSection(static_cast<SettingsSection>(section + 1));
                    selectedSettingsItem_ = 0;
                }
            } else {
                // Navigate content items
                int maxItems = 0;
                switch (state_.settingsSection()) {
                    case SettingsSection::Network: maxItems = 4; break;
                    case SettingsSection::Sharing: maxItems = (int)state_.sharedDirs().size(); break;
                    case SettingsSection::Identity: maxItems = 2; break;
                    case SettingsSection::Interface: maxItems = (int)builtinThemes().size(); break;
                }
                if (selectedSettingsItem_ < maxItems - 1) {
                    selectedSettingsItem_++;
                }
            }
            return true;
        }

        // Navigation down for other views
        if (view == View::Network) {
            int idx = state_.selectedPeerIndex();
            int maxIdx = state_.peers().size() - 1;
            if (maxIdx >= 0) {
                state_.setSelectedPeerIndex(std::min(maxIdx, idx + 1));
            }
            return true;
        } else if (view == View::Search) {
            int idx = state_.selectedSearchIndex();
            int maxIdx = state_.searchResults().size() - 1;
            if (maxIdx >= 0) {
                state_.setSelectedSearchIndex(std::min(maxIdx, idx + 1));
            }
            return true;
        } else if (view == View::Transfers) {
            int idx = state_.selectedTransferIndex();
            int maxIdx = state_.transfers().size() - 1;
            if (maxIdx >= 0) {
                state_.setSelectedTransferIndex(std::min(maxIdx, idx + 1));
            }
            return true;
        } else if (view == View::Browse) {
            int offset = (state_.browsePath() != "/") ? 1 : 0;
            int totalItems = state_.browseEntries().size() + offset;
            int idx = state_.selectedBrowseIndex();
            if (totalItems > 0) {
                state_.setSelectedBrowseIndex(std::min(totalItems - 1, idx + 1));
            }
            return true;
        } else if (view == View::Keys) {
            const auto& list = state_.showPendingKeys() ? state_.pendingKeys() : state_.trustedKeys();
            int idx = state_.selectedKeyIndex();
            int maxIdx = list.size() - 1;
            if (maxIdx >= 0) {
                state_.setSelectedKeyIndex(std::min(maxIdx, idx + 1));
            }
            return true;
        }
    }

    // Handle 'k' / ArrowUp for navigation up
    // Skip 'k' in Chat view - let the chat input handle it for typing
    if (event == Event::Character('k') || event == Event::ArrowUp) {
        std::lock_guard<std::mutex> lock(state_.mutex());
        View view = state_.currentView();

        // Settings navigation up
        if (view == View::Settings) {
            if (!settingsFocusContent_) {
                // Navigate sections
                int section = static_cast<int>(state_.settingsSection());
                if (section > 0) {
                    state_.setSettingsSection(static_cast<SettingsSection>(section - 1));
                    selectedSettingsItem_ = 0;
                }
            } else {
                // Navigate content items
                if (selectedSettingsItem_ > 0) {
                    selectedSettingsItem_--;
                }
            }
            return true;
        }

        if (view == View::Network) {
            int idx = state_.selectedPeerIndex();
            state_.setSelectedPeerIndex(std::max(0, idx - 1));
            return true;
        } else if (view == View::Search) {
            int idx = state_.selectedSearchIndex();
            state_.setSelectedSearchIndex(std::max(0, idx - 1));
            return true;
        } else if (view == View::Transfers) {
            int idx = state_.selectedTransferIndex();
            state_.setSelectedTransferIndex(std::max(0, idx - 1));
            return true;
        } else if (view == View::Browse) {
            int idx = state_.selectedBrowseIndex();
            state_.setSelectedBrowseIndex(std::max(0, idx - 1));
            return true;
        } else if (view == View::Keys) {
            int idx = state_.selectedKeyIndex();
            state_.setSelectedKeyIndex(std::max(0, idx - 1));
            return true;
        }
    }

    // Handle Ctrl+D for disconnect/delete/download/cancel
    if (event == Event::Character(CTRL_D)) {
        std::string dlHash, dlFilename;
        {
            std::lock_guard<std::mutex> lock(state_.mutex());
            View view = state_.currentView();

            if (view == View::Network) {
                int idx = state_.selectedPeerIndex();
                auto& peers = state_.peers();
                if (idx >= 0 && idx < (int)peers.size()) {
                    disconnectPeerName_ = peers[idx].nickname.empty() ?
                        peers[idx].address : peers[idx].nickname;
                    state_.setModalType("confirm_disconnect");
                    state_.setShowModal(true);
                }
                return true;
            } else if (view == View::Browse) {
                // Extract download info while locked; actual download call is after lock
                int offset = (state_.browsePath() != "/") ? 1 : 0;
                int idx = state_.selectedBrowseIndex() - offset;
                auto& entries = state_.browseEntries();
                if (idx >= 0 && idx < (int)entries.size() && !entries[idx].isDirectory) {
                    std::string peerGuid = state_.browsePeerGuid();
                    int fileId = entries[idx].fileId;
                    if (!peerGuid.empty() && fileId >= 0) {
                        dlHash = peerGuid + ":" + std::to_string(fileId);
                        dlFilename = entries[idx].name;
                    }
                }
                // Fall through to download outside lock
            } else if (view == View::Keys) {
                bool isPending = state_.showPendingKeys();
                int idx = state_.selectedKeyIndex();
                if (core_) {
                    auto keys = isPending ? core_->getPendingKeys() : core_->getTrustedKeys();
                    if (idx >= 0 && idx < (int)keys.size()) {
                        core_->removeKey(idx, isPending);
                        int newSize = (int)keys.size() - 1;
                        if (idx >= newSize && newSize > 0) {
                            state_.setSelectedKeyIndex(newSize - 1);
                        } else if (newSize == 0) {
                            state_.setSelectedKeyIndex(0);
                        }
                    }
                }
                return true;
            } else if (view == View::Settings &&
                       state_.settingsSection() == SettingsSection::Sharing &&
                       settingsFocusContent_) {
                int idx = selectedSettingsItem_;
                auto& dirs = state_.sharedDirs();
                if (idx >= 0 && idx < (int)dirs.size()) {
                    dirs.erase(dirs.begin() + idx);
                    if (core_) {
                        core_->removeSharedDirectory(idx);
                    }
                    if (idx >= (int)dirs.size() && !dirs.empty()) {
                        selectedSettingsItem_ = dirs.size() - 1;
                    }
                }
                return true;
            } else if (view == View::Transfers) {
                int idx = state_.selectedTransferIndex();
                auto& transfers = state_.transfers();
                if (idx >= 0 && idx < (int)transfers.size()) {
                    transfers.erase(transfers.begin() + idx);
                    if (idx > 0 && idx >= (int)transfers.size()) {
                        state_.setSelectedTransferIndex(idx - 1);
                    }
                }
                return true;
            } else {
                return false;
            }
        } // lock released here
        // Start download outside lock to avoid blocking UI
        if (core_ && !dlHash.empty()) {
            core_->downloadFile(dlHash, dlFilename);
        }
        return true;
    }

    // Handle Tab key for Keys view (switch between trusted/pending)
    if (event == Event::Tab) {
        std::lock_guard<std::mutex> lock(state_.mutex());
        if (state_.currentView() == View::Keys) {
            state_.setShowPendingKeys(!state_.showPendingKeys());
            state_.setSelectedKeyIndex(0);  // Reset selection when switching lists
            return true;
        }
    }

    // Handle Ctrl+T for trusting a pending key
    if (event == Event::Character(CTRL_T)) {
        std::lock_guard<std::mutex> lock(state_.mutex());
        if (state_.currentView() == View::Keys && state_.showPendingKeys()) {
            int idx = state_.selectedKeyIndex();
            if (core_) {
                auto pendingKeys = core_->getPendingKeys();
                if (idx >= 0 && idx < (int)pendingKeys.size()) {
                    core_->trustPendingKey(idx);
                    // Adjust selection
                    int newSize = (int)pendingKeys.size() - 1;
                    if (idx >= newSize && newSize > 0) {
                        state_.setSelectedKeyIndex(newSize - 1);
                    } else if (newSize == 0) {
                        state_.setSelectedKeyIndex(0);
                    }
                }
            }
            return true;
        }
    }

    // Handle Ctrl+E for exporting public key (Keys view)
    if (event == Event::Character(CTRL_E)) {
        std::lock_guard<std::mutex> lock(state_.mutex());
        if (state_.currentView() == View::Keys) {
            // Pre-fill with default export path
            if (core_) {
                modalPathInput_ = core_->getDefaultExportPath();
            }
            state_.setModalType("export_key");
            state_.setShowModal(true);
            return true;
        }
    }

    // Handle Ctrl+F for importing public key (Keys view)
    if (event == Event::Character(CTRL_F)) {
        std::lock_guard<std::mutex> lock(state_.mutex());
        if (state_.currentView() == View::Keys) {
            modalPathInput_.clear();
            state_.setModalType("import_key");
            state_.setShowModal(true);
            return true;
        }
    }

    // Handle Enter key
    if (event == Event::Return) {
        std::string dlHash, dlFilename;
        {
            std::lock_guard<std::mutex> lock(state_.mutex());
            View view = state_.currentView();

            if (view == View::Chat) {
                return false;
            } else if (view == View::Search) {
                return false;
            } else if (view == View::Browse) {
                int offset = (state_.browsePath() != "/") ? 1 : 0;
                int idx = state_.selectedBrowseIndex();
                std::string peer = state_.browsePeer();

                // Handle ".." (parent directory)
                if (offset > 0 && idx == 0) {
                    std::string path = state_.browsePath();
                    size_t pos = path.rfind('/');
                    std::string newPath;
                    if (pos != std::string::npos && pos > 0) {
                        newPath = path.substr(0, pos);
                    } else {
                        newPath = "/";
                    }
                    state_.setBrowsePath(newPath);
                    state_.setSelectedBrowseIndex(0);
                    state_.browseEntries().clear();
                    if (core_) {
                        core_->browsePeer(peer, newPath);
                    }
                    return true;
                }

                // Handle directory/file
                int entryIdx = idx - offset;
                auto& entries = state_.browseEntries();
                if (entryIdx >= 0 && entryIdx < (int)entries.size()) {
                    if (entries[entryIdx].isDirectory) {
                        // Enter directory — request listing from peer
                        std::string newPath = state_.browsePath();
                        if (newPath != "/") newPath += "/";
                        newPath += entries[entryIdx].name;
                        state_.setBrowsePath(newPath);
                        state_.setSelectedBrowseIndex(0);
                        state_.browseEntries().clear();
                        if (core_) {
                            core_->browsePeer(peer, newPath);
                        }
                    } else {
                        // Extract download info; call outside lock
                        std::string peerGuid = state_.browsePeerGuid();
                        int fileId = entries[entryIdx].fileId;
                        if (!peerGuid.empty() && fileId >= 0) {
                            dlHash = peerGuid + ":" + std::to_string(fileId);
                            dlFilename = entries[entryIdx].name;
                        }
                    }
                }
                // Fall through to download outside lock
            }
        } // lock released
        if (core_ && !dlHash.empty()) {
            core_->downloadFile(dlHash, dlFilename);
        }
        return true;
    }

    // Handle Ctrl+B for browse peer
    if (event == Event::Character(CTRL_B)) {
        std::lock_guard<std::mutex> lock(state_.mutex());
        View view = state_.currentView();

        if (view == View::Network) {
            int idx = state_.selectedPeerIndex();
            auto& peers = state_.peers();
            if (idx >= 0 && idx < (int)peers.size() &&
                peers[idx].status == ConnectionStatus::Online) {
                state_.setPreviousView(View::Network);
                state_.setBrowsePeer(peers[idx].nickname);
                state_.setBrowsePath("/");
                state_.setSelectedBrowseIndex(0);
                state_.browseEntries().clear();
                state_.rawBrowseEntries().clear();
                state_.setCurrentView(View::Browse);
                // Request file listing from peer
                if (core_) {
                    core_->browsePeer(peers[idx].nickname, "/");
                }
            }
            return true;
        } else if (view == View::Search) {
            int idx = state_.selectedSearchIndex();
            auto& results = state_.searchResults();
            if (idx >= 0 && idx < (int)results.size()) {
                state_.setPreviousView(View::Search);
                state_.setBrowsePeer(results[idx].user);
                state_.setBrowsePath("/");
                state_.setSelectedBrowseIndex(0);
                state_.browseEntries().clear();
                state_.rawBrowseEntries().clear();
                state_.setCurrentView(View::Browse);
                // Request file listing from peer
                if (core_) {
                    core_->browsePeer(results[idx].user, "/");
                }
            }
            return true;
        }
    }

    // Handle Tab to cycle through chat rooms (only in Chat view)
    if (event == Event::Tab) {
        std::lock_guard<std::mutex> lock(state_.mutex());
        View view = state_.currentView();
        if (view == View::Chat) {
            int numRooms = state_.chatRooms().size();
            if (numRooms > 0) {
                int idx = state_.selectedRoomIndex();
                state_.setSelectedRoomIndex((idx + 1) % numRooms);
            }
            return true;
        }
    }

    // Settings: horizontal navigation and editing
    if (event == Event::ArrowRight || event == Event::Character('l')) {
        std::lock_guard<std::mutex> lock(state_.mutex());
        if (state_.currentView() == View::Settings && !settingsFocusContent_) {
            settingsFocusContent_ = true;
            selectedSettingsItem_ = 0;
            return true;
        }
    }

    if (event == Event::ArrowLeft || event == Event::Character('h')) {
        std::lock_guard<std::mutex> lock(state_.mutex());
        if (state_.currentView() == View::Settings && settingsFocusContent_ && !settingsEditMode_) {
            settingsFocusContent_ = false;
            selectedSettingsItem_ = 0;
            return true;
        }
    }

    // Settings: Enter to edit, Space to toggle
    if (event == Event::Return) {
        std::lock_guard<std::mutex> lock(state_.mutex());
        if (state_.currentView() == View::Settings && settingsFocusContent_) {
            if (settingsEditMode_) {
                // Save the edit
                if (state_.settingsSection() == SettingsSection::Identity && selectedSettingsItem_ == 0) {
                    state_.setNickname(settingsEditBuffer_);
                    if (core_) {
                        core_->setNickname(settingsEditBuffer_);
                        core_->saveConfig();
                    }
                } else if (state_.settingsSection() == SettingsSection::Network && selectedSettingsItem_ == 0) {
                    int port = std::atoi(settingsEditBuffer_.c_str());
                    if (port > 0 && port < 65536) {
                        state_.setListenPort(port);
                        if (core_) {
                            core_->setListenPort(port);
                            core_->saveConfig();
                        }
                    }
                } else if (state_.settingsSection() == SettingsSection::Identity && selectedSettingsItem_ == 1) {
                    // Network name
                    state_.setNetworkHash(settingsEditBuffer_);
                    if (core_) {
                        core_->setNetworkName(settingsEditBuffer_);
                        core_->saveConfig();
                    }
                } else if (state_.settingsSection() == SettingsSection::Network && selectedSettingsItem_ == 1) {
                    // Upload limit value
                    int val = std::atoi(settingsEditBuffer_.c_str());
                    if (val > 0) {
                        state_.setUploadLimitKBps(val);
                        if (core_) {
                            core_->setThrottleUpload(state_.limitUpload(), val);
                            core_->saveConfig();
                        }
                    }
                } else if (state_.settingsSection() == SettingsSection::Network && selectedSettingsItem_ == 2) {
                    // Download limit value
                    int val = std::atoi(settingsEditBuffer_.c_str());
                    if (val > 0) {
                        state_.setDownloadLimitKBps(val);
                        if (core_) {
                            core_->setThrottleDownload(state_.limitDownload(), val);
                            core_->saveConfig();
                        }
                    }
                }
                settingsEditMode_ = false;
                settingsEditBuffer_.clear();
            } else if (state_.settingsSection() == SettingsSection::Interface) {
                // Select theme from list
                int idx = selectedSettingsItem_;
                if (idx >= 0 && idx < (int)builtinThemes().size()) {
                    state_.setThemeIndex(idx);
                    if (core_) {
                        core_->setThemeName(state_.theme().name);
                        core_->saveConfig();
                    }
                }
            } else {
                // Start editing
                if (state_.settingsSection() == SettingsSection::Identity && selectedSettingsItem_ == 0) {
                    settingsEditMode_ = true;
                    settingsEditBuffer_ = state_.nickname();
                } else if (state_.settingsSection() == SettingsSection::Network && selectedSettingsItem_ == 0) {
                    settingsEditMode_ = true;
                    settingsEditBuffer_ = std::to_string(state_.listenPort());
                } else if (state_.settingsSection() == SettingsSection::Identity && selectedSettingsItem_ == 1) {
                    settingsEditMode_ = true;
                    settingsEditBuffer_ = state_.networkHash();
                } else if (state_.settingsSection() == SettingsSection::Network && selectedSettingsItem_ == 1 && state_.limitUpload()) {
                    settingsEditMode_ = true;
                    settingsEditBuffer_ = std::to_string(state_.uploadLimitKBps());
                } else if (state_.settingsSection() == SettingsSection::Network && selectedSettingsItem_ == 2 && state_.limitDownload()) {
                    settingsEditMode_ = true;
                    settingsEditBuffer_ = std::to_string(state_.downloadLimitKBps());
                }
            }
            return true;
        }
    }

    // Settings: Space to toggle checkboxes
    if (event == Event::Character(' ')) {
        std::lock_guard<std::mutex> lock(state_.mutex());
        if (state_.currentView() == View::Settings && settingsFocusContent_) {
            if (state_.settingsSection() == SettingsSection::Interface) {
                int idx = selectedSettingsItem_;
                if (idx >= 0 && idx < (int)builtinThemes().size()) {
                    state_.setThemeIndex(idx);
                    if (core_) {
                        core_->setThemeName(state_.theme().name);
                        core_->saveConfig();
                    }
                }
                return true;
            } else if (state_.settingsSection() == SettingsSection::Network) {
                switch (selectedSettingsItem_) {
                    case 1: {
                        bool newVal = !state_.limitUpload();
                        state_.setLimitUpload(newVal);
                        if (core_) {
                            core_->setThrottleUpload(newVal, state_.uploadLimitKBps());
                            core_->saveConfig();
                        }
                        break;
                    }
                    case 2: {
                        bool newVal = !state_.limitDownload();
                        state_.setLimitDownload(newVal);
                        if (core_) {
                            core_->setThrottleDownload(newVal, state_.downloadLimitKBps());
                            core_->saveConfig();
                        }
                        break;
                    }
                    case 3: {
                        bool newVal = !state_.acceptIncoming();
                        state_.setAcceptIncoming(newVal);
                        if (core_) {
                            core_->setAcceptIncoming(newVal);
                            core_->saveConfig();
                        }
                        break;
                    }
                }
                return true;
            }
        }
    }

    // Settings: Escape to cancel edit or go back
    if (event == Event::Escape) {
        std::lock_guard<std::mutex> lock(state_.mutex());
        if (state_.currentView() == View::Settings) {
            if (settingsEditMode_) {
                settingsEditMode_ = false;
                settingsEditBuffer_.clear();
                return true;
            } else if (settingsFocusContent_) {
                settingsFocusContent_ = false;
                return true;
            }
        }
    }

    // Settings: handle character input when editing
    if (state_.currentView() == View::Settings && settingsEditMode_) {
        if (event == Event::Backspace) {
            if (!settingsEditBuffer_.empty()) {
                settingsEditBuffer_.pop_back();
            }
            return true;
        }
        if (event.is_character()) {
            settingsEditBuffer_ += event.character();
            return true;
        }
    }

    // Settings: Ctrl+S to save config
    if (event == Event::Character(CTRL_S)) {
        std::lock_guard<std::mutex> lock(state_.mutex());
        if (state_.currentView() == View::Settings && core_) {
            core_->saveConfig();
            return true;
        }
    }

    // Handle Shift+Tab to cycle backwards through chat rooms
    if (event == Event::TabReverse) {
        std::lock_guard<std::mutex> lock(state_.mutex());
        if (state_.currentView() == View::Chat) {
            int numRooms = state_.chatRooms().size();
            if (numRooms > 0) {
                int idx = state_.selectedRoomIndex();
                state_.setSelectedRoomIndex((idx - 1 + numRooms) % numRooms);
            }
            return true;
        }
    }

    // Handle Ctrl+P for pause transfer
    if (event == Event::Character(CTRL_P)) {
        std::lock_guard<std::mutex> lock(state_.mutex());
        if (state_.currentView() == View::Transfers) {
            int idx = state_.selectedTransferIndex();
            auto& transfers = state_.transfers();
            if (idx >= 0 && idx < (int)transfers.size() &&
                transfers[idx].status == TransferStatus::Active) {
                transfers[idx].status = TransferStatus::Paused;
            }
            return true;
        }
    }

    // Handle Ctrl+R for resume transfer, retry connection, or rescan
    if (event == Event::Character(CTRL_R)) {
        std::lock_guard<std::mutex> lock(state_.mutex());
        View view = state_.currentView();

        if (view == View::Settings &&
            state_.settingsSection() == SettingsSection::Sharing) {
            if (core_) {
                core_->rescanSharedDirectories();
            }
            return true;
        } else if (view == View::Transfers) {
            int idx = state_.selectedTransferIndex();
            auto& transfers = state_.transfers();
            if (idx >= 0 && idx < (int)transfers.size() &&
                transfers[idx].status == TransferStatus::Paused) {
                transfers[idx].status = TransferStatus::Active;
            }
            return true;
        } else if (view == View::Network) {
            int idx = state_.selectedPeerIndex();
            auto& peers = state_.peers();
            if (idx >= 0 && idx < (int)peers.size() &&
                peers[idx].status == ConnectionStatus::Failed) {
                peers[idx].status = ConnectionStatus::Connecting;
                peers[idx].errorMsg.clear();
            }
            return true;
        }
    }

    // Handle Ctrl+X for clear completed transfers
    if (event == Event::Character(CTRL_X)) {
        std::lock_guard<std::mutex> lock(state_.mutex());
        if (state_.currentView() == View::Transfers) {
            auto& transfers = state_.transfers();
            transfers.erase(
                std::remove_if(transfers.begin(), transfers.end(),
                    [](const TransferInfo& t) {
                        return t.status == TransferStatus::Completed ||
                               t.status == TransferStatus::Failed;
                    }),
                transfers.end());
            if (state_.selectedTransferIndex() >= (int)transfers.size()) {
                state_.setSelectedTransferIndex(std::max(0, (int)transfers.size() - 1));
            }
            return true;
        }
    }

    // Handle Backspace or 'h' for browse navigation (go up)
    if (event == Event::Backspace ||
        (event == Event::Character('h') && state_.currentView() == View::Browse)) {
        std::lock_guard<std::mutex> lock(state_.mutex());
        if (state_.currentView() == View::Browse && state_.browsePath() != "/") {
            std::string path = state_.browsePath();
            size_t pos = path.rfind('/');
            std::string newPath;
            if (pos != std::string::npos && pos > 0) {
                newPath = path.substr(0, pos);
            } else {
                newPath = "/";
            }
            state_.setBrowsePath(newPath);
            state_.setSelectedBrowseIndex(0);
            state_.browseEntries().clear();
            std::string peer = state_.browsePeer();
            if (core_) {
                core_->browsePeer(peer, newPath);
            }
            return true;
        }
    }

    return false;
}

Component App::buildAddConnectionModal() {
    return components::TwoFieldModal(
        "Add Connection",
        "Address", &modalAddressInput_,
        "Port", &modalPortInput_,
        [this]() {
            std::lock_guard<std::mutex> lock(state_.mutex());
            if (!modalAddressInput_.empty()) {
                PeerInfo peer;
                peer.address = modalAddressInput_;
                try {
                    peer.port = std::stoi(modalPortInput_.empty() ? "4001" : modalPortInput_);
                } catch (...) {
                    peer.port = 4001;
                }
                peer.status = ConnectionStatus::Connecting;
                peer.filesShared = 0;
                peer.connectedAt = std::chrono::steady_clock::now();
                state_.peers().push_back(peer);
            }
            state_.setShowModal(false);
            modalAddressInput_.clear();
        },
        [this]() {
            std::lock_guard<std::mutex> lock(state_.mutex());
            state_.setShowModal(false);
            modalAddressInput_.clear();
        }
    );
}

Component App::buildConfirmDisconnectModal() {
    return components::ConfirmModal(
        "Disconnect",
        "Disconnect from " + disconnectPeerName_ + "?",
        [this]() {
            std::lock_guard<std::mutex> lock(state_.mutex());
            int idx = state_.selectedPeerIndex();
            auto& peers = state_.peers();
            if (idx >= 0 && idx < (int)peers.size()) {
                peers.erase(peers.begin() + idx);
                if (idx > 0 && idx >= (int)peers.size()) {
                    state_.setSelectedPeerIndex(idx - 1);
                }
            }
            state_.setShowModal(false);
        },
        [this]() {
            std::lock_guard<std::mutex> lock(state_.mutex());
            state_.setShowModal(false);
        }
    );
}

Component App::buildJoinRoomModal() {
    return components::InputModal(
        "Join Room",
        "Room name",
        &modalRoomInput_,
        [this]() {
            std::lock_guard<std::mutex> lock(state_.mutex());
            if (!modalRoomInput_.empty()) {
                std::string roomName = modalRoomInput_;
                if (roomName[0] != '#') roomName = "#" + roomName;

                // Check if already joined
                bool found = false;
                for (const auto& room : state_.chatRooms()) {
                    if (room.name == roomName) {
                        found = true;
                        break;
                    }
                }

                if (!found) {
                    ChatRoom newRoom;
                    newRoom.name = roomName;
                    newRoom.isDirect = false;
                    newRoom.unreadCount = 0;
                    state_.chatRooms().push_back(newRoom);
                    state_.setSelectedRoomIndex(state_.chatRooms().size() - 1);
                }
            }
            state_.setShowModal(false);
            modalRoomInput_.clear();
        },
        [this]() {
            std::lock_guard<std::mutex> lock(state_.mutex());
            state_.setShowModal(false);
            modalRoomInput_.clear();
        }
    );
}

Component App::buildAddDirectoryModal() {
    return components::InputModal(
        "Add Shared Directory",
        "Path",
        &modalPathInput_,
        [this]() {
            std::lock_guard<std::mutex> lock(state_.mutex());
            if (!modalPathInput_.empty()) {
                SharedDirectory dir;
                dir.path = modalPathInput_;
                dir.fileCount = 0;
                dir.totalSize = 0;
                dir.scanning = true;
                state_.sharedDirs().push_back(dir);
                // Tell core to add this directory
                if (core_) {
                    core_->addSharedDirectory(modalPathInput_);
                }
            }
            state_.setShowModal(false);
            modalPathInput_.clear();
        },
        [this]() {
            std::lock_guard<std::mutex> lock(state_.mutex());
            state_.setShowModal(false);
            modalPathInput_.clear();
        }
    );
}

Component App::buildDownloadConfirmModal() {
    return Renderer([this] {
        std::lock_guard<std::mutex> lock(state_.mutex());
        int idx = state_.selectedSearchIndex();
        std::string filename = "Unknown";
        std::string sizeStr = "0 B";
        std::string sources = "0";

        if (idx >= 0 && idx < (int)state_.searchResults().size()) {
            const auto& result = state_.searchResults()[idx];
            filename = result.filename;
            sizeStr = formatSize(result.size);
            sources = std::to_string(result.sources);
        }

        return window(
            text(" Download File ") | bold,
            vbox({
                text(filename) | bold,
                text(""),
                hbox({text("Size: "), text(sizeStr)}),
                hbox({text("Sources: "), text(sources + " peers")}),
                text(""),
                separator(),
                hbox({
                    filler(),
                    text("[Download]") | border | bold,
                    text("  "),
                    text("[Cancel]") | border,
                    filler()
                })
            }) | ftxui::size(WIDTH, GREATER_THAN, 40)
        ) | clear_under | center;
    }) | CatchEvent([this](Event event) {
        if (event == Event::Return || event == Event::Character('d')) {
            std::lock_guard<std::mutex> lock(state_.mutex());
            int idx = state_.selectedSearchIndex();
            if (idx >= 0 && idx < (int)state_.searchResults().size()) {
                const auto& result = state_.searchResults()[idx];
                // Create transfer
                TransferInfo xfer;
                xfer.id = state_.transfers().size() + 1;
                xfer.filename = result.filename;
                xfer.direction = TransferDirection::Download;
                xfer.status = TransferStatus::Active;
                xfer.totalSize = result.size;
                xfer.transferred = 0;
                xfer.speedKBps = 0;
                xfer.peer = result.user;
                state_.transfers().push_back(xfer);
            }
            state_.setShowModal(false);
            return true;
        }
        if (event == Event::Escape || event == Event::Character('c')) {
            std::lock_guard<std::mutex> lock(state_.mutex());
            state_.setShowModal(false);
            return true;
        }
        return false;
    });
}

Component App::buildNetworkView() {
    return Renderer([this] {
        std::lock_guard<std::mutex> lock(state_.mutex());

        if (state_.peers().empty()) {
            return vbox({
                text(" Network ") | bold,
                separator(),
                filler(),
                text("No connections") | dim | center,
                text("") | center,
                text("Press Ctrl+A to add a connection") | dim | center,
                filler()
            }) | flex | border;
        }

        std::vector<std::vector<std::string>> rows;
        for (const auto& peer : state_.peers()) {
            std::string addr = peer.address + ":" + std::to_string(peer.port);
            std::string nick = peer.nickname.empty() ? "—" : peer.nickname;
            std::string files = peer.status == ConnectionStatus::Online
                ? std::to_string(peer.filesShared) : "—";
            std::string duration = peer.status == ConnectionStatus::Online
                ? formatDuration(peer.connectedAt)
                : (peer.status == ConnectionStatus::Failed ? peer.errorMsg : "—");

            rows.push_back({
                connectionStatusStr(peer.status),
                addr,
                nick,
                files,
                duration
            });
        }

        std::vector<components::TableColumn> columns = {
            {"STATUS", 10},
            {"ADDRESS", 22},
            {"NICK", 12},
            {"FILES", 8, components::TableColumn::Align::Right},
            {"CONNECTED", 12}
        };

        int selected = state_.selectedPeerIndex();
        auto table = components::TableElement(rows, columns, selected, true, state_.theme().accent);

        return vbox({
            text(" Network ") | bold,
            separator(),
            table | flex
        }) | flex | border;
    }) | CatchEvent([this](Event event) {
        std::lock_guard<std::mutex> lock(state_.mutex());
        auto& peers = state_.peers();

        // Always allow add
        if (event == Event::Character(CTRL_A)) {
            state_.setModalType("add_connection");
            state_.setShowModal(true);
            return true;
        }

        if (peers.empty()) return false;

        int idx = state_.selectedPeerIndex();
        int maxIdx = peers.size() - 1;

        // Navigation
        if (event == Event::ArrowUp || event == Event::Character('k')) {
            state_.setSelectedPeerIndex(std::max(0, idx - 1));
            return true;
        }
        if (event == Event::ArrowDown || event == Event::Character('j')) {
            state_.setSelectedPeerIndex(std::min(maxIdx, idx + 1));
            return true;
        }
        if (event == Event::Home) {
            state_.setSelectedPeerIndex(0);
            return true;
        }
        if (event == Event::End) {
            state_.setSelectedPeerIndex(maxIdx);
            return true;
        }

        // Actions
        if (event == Event::Character(CTRL_D)) {
            if (idx >= 0 && idx < (int)peers.size()) {
                disconnectPeerName_ = peers[idx].nickname.empty() ?
                    peers[idx].address : peers[idx].nickname;
                state_.setModalType("confirm_disconnect");
                state_.setShowModal(true);
            }
            return true;
        }
        // Note: Ctrl+B for browse is handled by handleGlobalEvent
        if (event == Event::Character(CTRL_N)) {
            if (idx >= 0 && idx < (int)peers.size() &&
                peers[idx].status == ConnectionStatus::Online) {
                // Open or create DM
                std::string nick = peers[idx].nickname;
                std::string dmRoom = "@" + nick;
                bool found = false;
                for (size_t i = 0; i < state_.chatRooms().size(); ++i) {
                    if (state_.chatRooms()[i].isDirect &&
                        state_.chatRooms()[i].name == dmRoom) {
                        state_.setSelectedRoomIndex(i);
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    ChatRoom dm;
                    dm.name = dmRoom;
                    dm.isDirect = true;
                    dm.unreadCount = 0;
                    state_.chatRooms().push_back(dm);
                    state_.setSelectedRoomIndex(state_.chatRooms().size() - 1);
                }
                state_.setCurrentView(View::Chat);
                tabIndex_ = 3;
            }
            return true;
        }
        if (event == Event::Character(CTRL_R)) {
            // Retry failed connection
            if (idx >= 0 && idx < (int)peers.size() &&
                peers[idx].status == ConnectionStatus::Failed) {
                peers[idx].status = ConnectionStatus::Connecting;
                peers[idx].errorMsg.clear();
            }
            return true;
        }

        return false;
    });
}

Component App::buildSearchView() {
    auto inputOption = InputOption();
    inputOption.on_enter = [this] {
        // Trigger search when Enter is pressed in the input
        std::string query;
        {
            std::lock_guard<std::mutex> lock(state_.mutex());
            query = state_.searchQuery();
            // Clear previous results
            state_.searchResults().clear();
            state_.setSelectedSearchIndex(0);
        }
        fprintf(stderr, "[UI] Search on_enter: query='%s' core_=%p isRunning=%d\n",
            query.c_str(), (void*)core_.get(), core_ ? core_->isRunning() : -1);
        if (!query.empty() && core_ && core_->isRunning()) {
            fprintf(stderr, "[UI] Calling core_->search()\n");
            core_->search(query);
        } else {
            fprintf(stderr, "[UI] Search not triggered: empty=%d core=%p running=%d\n",
                query.empty(), (void*)core_.get(), core_ ? core_->isRunning() : -1);
        }
    };
    auto searchInput = Input(&state_.searchQuery(), "Enter search query...", inputOption);

    return Container::Vertical({
        searchInput
    }) | Renderer([this, searchInput](Element inner) {
        std::lock_guard<std::mutex> lock(state_.mutex());

        if (state_.searchResults().empty()) {
            return vbox({
                text(" Search ") | bold,
                separator(),
                hbox({
                    text(" Query: "),
                    searchInput->Render() | flex | border
                }),
                separator(),
                filler(),
                text(state_.searchQuery().empty() ?
                    "Enter a search query above" : "No results found") | dim | center,
                text("") | center,
                text("Press '/' or Ctrl+N to focus search") | dim | center,
                filler()
            }) | flex | border;
        }

        std::vector<std::vector<std::string>> rows;
        for (const auto& result : state_.searchResults()) {
            rows.push_back({
                result.filename,
                formatSize(result.size),
                result.type,
                std::to_string(result.sources),
                result.user
            });
        }

        std::vector<components::TableColumn> columns = {
            {"NAME", 35},
            {"SIZE", 10, components::TableColumn::Align::Right},
            {"TYPE", 6},
            {"SRCS", 5, components::TableColumn::Align::Right},
            {"USER", 12}
        };

        int selected = state_.selectedSearchIndex();
        auto table = components::TableElement(rows, columns, selected, true, state_.theme().accent);

        return vbox({
            text(" Search ") | bold,
            separator(),
            hbox({
                text(" Query: "),
                searchInput->Render() | flex | border
            }),
            separator(),
            text(" Results: " + std::to_string(state_.searchResults().size()) + " files") | dim,
            table | flex
        }) | flex | border;
    }) | CatchEvent([this](Event event) {
        // Handle Enter key - always trigger search
        if (event == Event::Return) {
            std::string query;
            {
                std::lock_guard<std::mutex> lock(state_.mutex());
                query = state_.searchQuery();
                // Clear previous results for new search
                state_.searchResults().clear();
                state_.setSelectedSearchIndex(0);
            }
            fprintf(stderr, "[UI] Search Enter pressed: query='%s' core_=%p isRunning=%d\n",
                query.c_str(), (void*)core_.get(), core_ ? core_->isRunning() : -1);
            if (!query.empty() && core_ && core_->isRunning()) {
                fprintf(stderr, "[UI] Calling core_->search()\n");
                core_->search(query);
            }
            return true;  // Consume Enter
        }

        // Escape cancels search and clears results
        if (event == Event::Escape) {
            if (core_) {
                core_->cancelSearch();
            }
            {
                std::lock_guard<std::mutex> lock(state_.mutex());
                state_.searchResults().clear();
                state_.setSelectedSearchIndex(0);
            }
            return true;
        }

        std::lock_guard<std::mutex> lock(state_.mutex());
        auto& results = state_.searchResults();

        // Focus search input
        if (event == Event::Character('/') || event == Event::Character(CTRL_N)) {
            return false;  // Let input handle it
        }

        if (results.empty()) return false;

        int idx = state_.selectedSearchIndex();
        int maxIdx = results.size() - 1;

        // Navigation
        if (event == Event::ArrowUp || event == Event::Character('k')) {
            state_.setSelectedSearchIndex(std::max(0, idx - 1));
            return true;
        }
        if (event == Event::ArrowDown || event == Event::Character('j')) {
            state_.setSelectedSearchIndex(std::min(maxIdx, idx + 1));
            return true;
        }
        if (event == Event::Home) {
            state_.setSelectedSearchIndex(0);
            return true;
        }
        if (event == Event::End) {
            state_.setSelectedSearchIndex(maxIdx);
            return true;
        }

        // Download (use Ctrl+D, Enter is reserved for search input)
        if (event == Event::Character(CTRL_D)) {
            fprintf(stderr, "[UI] 'd' pressed for download, idx=%d, results=%zu\n",
                idx, results.size());
            state_.setModalType("download_confirm");
            state_.setShowModal(true);
            return true;
        }

        // Note: 'b' for browse is handled by handleGlobalEvent

        return false;
    });
}

Component App::buildTransfersView() {
    return Renderer([this] {
        std::lock_guard<std::mutex> lock(state_.mutex());

        std::vector<TransferInfo*> downloads;
        std::vector<TransferInfo*> uploads;

        for (auto& xfer : state_.transfers()) {
            if (xfer.direction == TransferDirection::Download) {
                downloads.push_back(&xfer);
            } else {
                uploads.push_back(&xfer);
            }
        }

        const auto& th = state_.theme();
        auto renderTransfer = [&th](const TransferInfo& xfer, bool selected) {
            float progress = xfer.totalSize > 0 ?
                (float)xfer.transferred / xfer.totalSize : 0.0f;

            std::string status;
            Color statusColor = Color::Default;
            switch (xfer.status) {
                case TransferStatus::Active:
                    status = formatSpeed(xfer.speedKBps);
                    statusColor = th.success;
                    break;
                case TransferStatus::Paused:
                    status = "PAUSED";
                    statusColor = th.warning;
                    break;
                case TransferStatus::Queued:
                    status = "QUEUED";
                    statusColor = th.primary;
                    break;
                case TransferStatus::Completed:
                    status = "DONE";
                    statusColor = th.success;
                    break;
                case TransferStatus::Failed:
                    status = "FAILED";
                    statusColor = th.error;
                    break;
            }

            std::string peerLabel = xfer.direction == TransferDirection::Download
                ? "from: " : "to: ";

            auto entry = vbox({
                hbox({
                    text(selected ? "▸ " : "  "),
                    text(xfer.filename) | bold,
                    filler(),
                    text(peerLabel + xfer.peer) | dim
                }),
                hbox({
                    text("  "),
                    gauge(progress) | flex | color(th.accent),
                    text(" "),
                    text(std::to_string((int)(progress * 100)) + "%") | size(WIDTH, EQUAL, 4),
                    text(" "),
                    text(formatSize(xfer.transferred) + "/" + formatSize(xfer.totalSize)),
                    text(" "),
                    text(status) | color(statusColor)
                })
            });

            if (selected) {
                entry = entry | bgcolor(th.bgDark);
            }

            return entry;
        };

        // Empty state
        if (state_.transfers().empty()) {
            return vbox({
                text(" Transfers ") | bold,
                separator(),
                filler(),
                text("No active transfers") | dim | center,
                text("") | center,
                text("Search for files to download") | dim | center,
                filler()
            }) | flex | border;
        }

        Elements downloadElems;
        Elements uploadElems;
        int currentIdx = 0;
        int selected = state_.selectedTransferIndex();

        for (auto* xfer : downloads) {
            downloadElems.push_back(renderTransfer(*xfer, currentIdx == selected));
            downloadElems.push_back(text(""));
            currentIdx++;
        }

        for (auto* xfer : uploads) {
            uploadElems.push_back(renderTransfer(*xfer, currentIdx == selected));
            uploadElems.push_back(text(""));
            currentIdx++;
        }

        return vbox({
            text(" Transfers ") | bold,
            separator(),
            text(" DOWNLOADS (" + std::to_string(downloads.size()) + ")") | dim,
            vbox(downloadElems) | flex,
            separator(),
            text(" UPLOADS (" + std::to_string(uploads.size()) + ")") | dim,
            vbox(uploadElems) | flex
        }) | flex | border;
    }) | CatchEvent([this](Event event) {
        std::lock_guard<std::mutex> lock(state_.mutex());
        auto& transfers = state_.transfers();

        if (transfers.empty()) return false;

        int idx = state_.selectedTransferIndex();
        int maxIdx = transfers.size() - 1;

        // Navigation
        if (event == Event::ArrowUp || event == Event::Character('k')) {
            state_.setSelectedTransferIndex(std::max(0, idx - 1));
            return true;
        }
        if (event == Event::ArrowDown || event == Event::Character('j')) {
            state_.setSelectedTransferIndex(std::min(maxIdx, idx + 1));
            return true;
        }

        // Pause
        if (event == Event::Character(CTRL_P)) {
            if (idx >= 0 && idx < (int)transfers.size()) {
                if (transfers[idx].status == TransferStatus::Active) {
                    transfers[idx].status = TransferStatus::Paused;
                }
            }
            return true;
        }

        // Resume
        if (event == Event::Character(CTRL_R)) {
            if (idx >= 0 && idx < (int)transfers.size()) {
                if (transfers[idx].status == TransferStatus::Paused) {
                    transfers[idx].status = TransferStatus::Active;
                }
            }
            return true;
        }

        // Cancel
        if (event == Event::Character(CTRL_D)) {
            if (idx >= 0 && idx < (int)transfers.size()) {
                transfers.erase(transfers.begin() + idx);
                if (idx > maxIdx - 1) {
                    state_.setSelectedTransferIndex(std::max(0, idx - 1));
                }
            }
            return true;
        }

        // Clear completed
        if (event == Event::Character(CTRL_X)) {
            transfers.erase(
                std::remove_if(transfers.begin(), transfers.end(),
                    [](const TransferInfo& t) {
                        return t.status == TransferStatus::Completed;
                    }),
                transfers.end()
            );
            if (state_.selectedTransferIndex() >= (int)transfers.size()) {
                state_.setSelectedTransferIndex(std::max(0, (int)transfers.size() - 1));
            }
            return true;
        }

        return false;
    });
}

Component App::buildChatView() {
    auto chatInput = Input(&state_.chatInput(), "Type message...");

    return Container::Vertical({
        chatInput
    }) | Renderer([this, chatInput](Element inner) {
        std::lock_guard<std::mutex> lock(state_.mutex());

        // Room list
        Elements roomList;
        roomList.push_back(text(" ROOMS") | bold | dim);
        roomList.push_back(separator());

        const auto& th = state_.theme();

        int roomIdx = 0;
        for (size_t i = 0; i < state_.chatRooms().size(); ++i) {
            const auto& room = state_.chatRooms()[i];
            if (room.isDirect) continue;

            std::string label = room.name;
            if (room.unreadCount > 0) {
                label += " (" + std::to_string(room.unreadCount) + ")";
            }

            bool selected = (int)i == state_.selectedRoomIndex();
            auto roomLine = text((selected ? "▸" : " ") + label);
            if (selected) {
                roomLine = roomLine | bold | color(th.accent);
            }
            if (room.unreadCount > 0) {
                roomLine = roomLine | bold;
            }
            roomList.push_back(roomLine);
            roomIdx++;
        }

        roomList.push_back(separator());
        roomList.push_back(text(" DIRECT") | bold | dim);

        for (size_t i = 0; i < state_.chatRooms().size(); ++i) {
            const auto& room = state_.chatRooms()[i];
            if (!room.isDirect) continue;

            std::string label = " " + room.name;
            if (room.unreadCount > 0) {
                label += " (" + std::to_string(room.unreadCount) + ")";
            }

            bool selected = (int)i == state_.selectedRoomIndex();
            auto roomLine = text((selected ? "▸" : " ") + label);
            if (selected) {
                roomLine = roomLine | bold | color(th.accent);
            }
            if (room.unreadCount > 0) {
                roomLine = roomLine | bold;
            }
            roomList.push_back(roomLine);
        }

        // Messages
        Elements messages;
        std::string roomTitle = "(no room selected)";

        if (state_.selectedRoomIndex() >= 0 &&
            state_.selectedRoomIndex() < (int)state_.chatRooms().size()) {
            const auto& room = state_.chatRooms()[state_.selectedRoomIndex()];
            roomTitle = room.name;

            if (room.messages.empty()) {
                messages.push_back(filler());
                messages.push_back(text("No messages yet") | dim | center);
                messages.push_back(filler());
            } else {
                for (const auto& msg : room.messages) {
                    auto time = std::chrono::system_clock::to_time_t(msg.timestamp);
                    std::tm tm = *std::localtime(&time);
                    std::ostringstream oss;
                    oss << std::put_time(&tm, "[%H:%M] ");

                    Element line;
                    if (msg.isSystem) {
                        // System messages: show content (join/leave/etc)
                        line = hbox({
                            text(oss.str()),
                            renderMessageContent(msg.content, th.primary)
                        }) | dim;
                    } else {
                        // Regular messages: show sender and content
                        std::string senderName = msg.sender.empty() ? "anon" : msg.sender;
                        line = hbox({
                            text(oss.str()) | dim,
                            text(senderName + ": ") | bold | color(th.accent),
                            renderMessageContent(msg.content, th.primary)
                        });
                    }
                    messages.push_back(line);
                }
            }
        }

        // Auto-scroll to bottom by focusing on last message
        Element messagesBox;
        if (messages.empty()) {
            messagesBox = vbox(messages) | flex | yframe;
        } else {
            // Add focus marker to last message to auto-scroll
            messages.back() = messages.back() | focus;
            messagesBox = vbox(messages) | flex | yframe | focusPositionRelative(0, 1);
        }

        return hbox({
            vbox(roomList) | size(WIDTH, EQUAL, 15) | border,
            vbox({
                text(" " + roomTitle) | bold,
                separator(),
                messagesBox,
                separator(),
                hbox({text(" > "), chatInput->Render() | flex})
            }) | flex | border
        }) | flex;
    }) | CatchEvent([this, chatInput](Event event) {
        // Send message on Enter
        if (event == Event::Return) {
            std::string room;
            std::string message;
            int roomIdx = -1;
            int roomCount = 0;
            bool inputEmpty = false;
            {
                std::lock_guard<std::mutex> lock(state_.mutex());
                inputEmpty = state_.chatInput().empty();
                roomIdx = state_.selectedRoomIndex();
                roomCount = (int)state_.chatRooms().size();
                if (inputEmpty) {
                    fprintf(stderr, "[CHAT-UI] Enter pressed but input empty\n");
                    return false;
                }
                if (roomIdx >= 0 && roomIdx < roomCount) {
                    room = state_.chatRooms()[roomIdx].name;
                    message = state_.chatInput();
                    state_.chatInput().clear();
                } else {
                    fprintf(stderr, "[CHAT-UI] Enter pressed but no valid room: idx=%d count=%d\n",
                        roomIdx, roomCount);
                }
            }
            // Call core outside of lock to avoid deadlock
            if (!room.empty() && !message.empty() && core_ && core_->isRunning()) {
                fprintf(stderr, "[CHAT-UI] Calling sendChatMessage room='%s' msg='%s'\n",
                    room.c_str(), message.c_str());
                core_->sendChatMessage(room, message);
            } else {
                fprintf(stderr, "[CHAT-UI] NOT sending: room='%s' msg='%s' core=%p running=%d\n",
                    room.c_str(), message.c_str(), (void*)core_.get(),
                    core_ ? core_->isRunning() : 0);
            }
            return true;
        }

        // Room navigation — arrow keys always work (no j/k in Chat)
        if (event == Event::ArrowUp) {
            std::lock_guard<std::mutex> lock(state_.mutex());
            int idx = state_.selectedRoomIndex();
            if (idx > 0) state_.setSelectedRoomIndex(idx - 1);
            return true;
        }
        if (event == Event::ArrowDown) {
            std::lock_guard<std::mutex> lock(state_.mutex());
            int idx = state_.selectedRoomIndex();
            if (idx < (int)state_.chatRooms().size() - 1)
                state_.setSelectedRoomIndex(idx + 1);
            return true;
        }

        // Ctrl+O: Join room
        if (event == Event::Character(CTRL_O)) {
            std::lock_guard<std::mutex> lock(state_.mutex());
            state_.setModalType("join_room");
            state_.setShowModal(true);
            return true;
        }

        // Ctrl+N: Start DM
        if (event == Event::Character(CTRL_N)) {
            std::lock_guard<std::mutex> lock(state_.mutex());
            state_.setModalType("start_dm");
            state_.setShowModal(true);
            return true;
        }

        // Ctrl+L: Leave room
        if (event == Event::Character(CTRL_L)) {
            std::lock_guard<std::mutex> lock(state_.mutex());
            int idx = state_.selectedRoomIndex();
            if (idx > 0 && idx < (int)state_.chatRooms().size()) {
                state_.chatRooms().erase(state_.chatRooms().begin() + idx);
                state_.setSelectedRoomIndex(std::max(0, idx - 1));
            }
            return true;
        }

        return false;
    });
}

Component App::buildBrowseView() {
    return Renderer([this] {
        std::lock_guard<std::mutex> lock(state_.mutex());

        std::vector<std::vector<std::string>> rows;

        // Parent directory (if not at root)
        if (state_.browsePath() != "/") {
            rows.push_back({"..", "<parent>"});
        }

        for (const auto& entry : state_.browseEntries()) {
            std::string sizeStr = entry.isDirectory ? "<dir>" : formatSize(entry.size);
            std::string name = entry.isDirectory ? entry.name + "/" : entry.name;
            rows.push_back({name, sizeStr});
        }

        // Empty state
        if (rows.empty()) {
            return vbox({
                text(" Browse: " + state_.browsePeer() + " ") | bold,
                separator(),
                text(" Location: " + state_.browsePath()) | dim,
                separator(),
                filler(),
                text("(empty directory)") | dim | center,
                filler()
            }) | flex | border;
        }

        std::vector<components::TableColumn> columns = {
            {"NAME", 45},
            {"SIZE", 12, components::TableColumn::Align::Right}
        };

        int selected = state_.selectedBrowseIndex();
        auto table = components::TableElement(rows, columns, selected, true, state_.theme().accent);

        return vbox({
            text(" Browse: " + state_.browsePeer() + " ") | bold,
            separator(),
            text(" Location: " + state_.browsePath()) | dim,
            separator(),
            table | flex
        }) | flex | border;
    }) | CatchEvent([this](Event event) {
        // Note: Enter, 'd', Escape, Backspace, j/k, arrow keys are all handled
        // by handleGlobalEvent. This CatchEvent handles 'l' (right arrow alias)
        // for entering directories as a vim-like navigation alternative.
        if (event == Event::Character('l')) {
            std::lock_guard<std::mutex> lock(state_.mutex());
            auto& entries = state_.browseEntries();
            int offset = (state_.browsePath() != "/") ? 1 : 0;
            int idx = state_.selectedBrowseIndex();
            std::string peer = state_.browsePeer();

            // ".." selected - go up
            if (offset > 0 && idx == 0) {
                std::string path = state_.browsePath();
                size_t pos = path.rfind('/');
                std::string newPath = (pos != std::string::npos && pos > 0) ?
                    path.substr(0, pos) : "/";
                state_.setBrowsePath(newPath);
                state_.setSelectedBrowseIndex(0);
                state_.browseEntries().clear();
                if (core_) {
                    core_->browsePeer(peer, newPath);
                }
            } else {
                int entryIdx = idx - offset;
                if (entryIdx >= 0 && entryIdx < (int)entries.size() && entries[entryIdx].isDirectory) {
                    std::string newPath = state_.browsePath();
                    if (newPath != "/") newPath += "/";
                    newPath += entries[entryIdx].name;
                    state_.setBrowsePath(newPath);
                    state_.setSelectedBrowseIndex(0);
                    state_.browseEntries().clear();
                    if (core_) {
                        core_->browsePeer(peer, newPath);
                    }
                }
            }
            return true;
        }

        return false;
    });
}

Component App::buildKeysView() {
    return Renderer([this] {
        bool showPending;
        int selectedIdx;
        {
            std::lock_guard<std::mutex> lock(state_.mutex());
            showPending = state_.showPendingKeys();
            selectedIdx = state_.selectedKeyIndex();
        }

        // Fetch keys from core (already thread-safe internally)
        std::vector<KeyInfo> trustedKeys = core_ ? core_->getTrustedKeys() : std::vector<KeyInfo>();
        std::vector<KeyInfo> pendingKeys = core_ ? core_->getPendingKeys() : std::vector<KeyInfo>();

        // Get current list
        const auto& currentList = showPending ? pendingKeys : trustedKeys;

        const auto& th = state_.theme();

        // Header: Own identity
        std::string ownFingerprint = core_ ? core_->getPublicKeyHash() : "No key loaded";
        auto ownKeyBox = window(text(" Your Identity "), vbox({
            hbox({text("Fingerprint: ") | bold, text(ownFingerprint) | color(th.accent)}),
        })) | size(HEIGHT, EQUAL, 3);

        // Tab selector: Trusted vs Pending
        auto trustedTab = text(" Trusted ") | (showPending ? dim : bold);
        auto pendingTab = text(" Pending ") | (showPending ? bold : dim);
        if (!showPending) trustedTab = trustedTab | bgcolor(th.primary) | color(th.primaryFg);
        else pendingTab = pendingTab | bgcolor(th.warning) | color(th.contrastFg);

        auto tabRow = hbox({
            trustedTab | border,
            pendingTab | border,
            filler(),
            text("[Tab] Switch Lists") | dim
        });

        // Key list
        Elements keyRows;
        if (currentList.empty()) {
            keyRows.push_back(text(showPending ? "  No pending keys" : "  No trusted keys") | dim);
        } else {
            for (size_t i = 0; i < currentList.size(); ++i) {
                const auto& key = currentList[i];
                std::string bitsStr = std::to_string(key.bits) + "-bit";

                auto nameEl = text(key.name.empty() ? "(unknown)" : key.name);
                auto fpEl = text(key.fingerprint) | dim;
                auto bitsEl = text(bitsStr) | color(th.success);

                auto row = hbox({
                    text("  "),
                    nameEl | size(WIDTH, EQUAL, 20),
                    text(" "),
                    fpEl | size(WIDTH, EQUAL, 44),
                    text(" "),
                    bitsEl
                });

                if ((int)i == selectedIdx) {
                    row = row | bgcolor(th.primary) | color(th.primaryFg);
                }

                keyRows.push_back(row);
            }
        }

        auto keyListBox = window(
            text(showPending ? " Pending Keys " : " Trusted Keys "),
            vbox(keyRows) | vscroll_indicator | frame
        ) | flex;

        // Actions for selected key
        Elements actions;
        if (!currentList.empty() && selectedIdx >= 0 && selectedIdx < (int)currentList.size()) {
            if (showPending) {
                actions.push_back(text("[t] Trust  "));
                actions.push_back(text("[d] Delete  "));
            } else {
                actions.push_back(text("[d] Delete  "));
                actions.push_back(text("[e] Export  "));
            }
        }
        actions.push_back(text("[i] Import Key") | dim);

        auto actionsRow = hbox(actions);

        return vbox({
            ownKeyBox,
            tabRow,
            keyListBox,
            separator(),
            actionsRow
        });
    });
}

Component App::buildSettingsView() {
    return Renderer([this] {
        std::lock_guard<std::mutex> lock(state_.mutex());

        const auto& th = state_.theme();

        std::vector<std::string> sections = {"Network", "Sharing", "Identity", "Interface"};
        Elements sectionList;
        sectionList.push_back(text(" SECTION") | bold | dim);
        sectionList.push_back(separator());

        for (size_t i = 0; i < sections.size(); ++i) {
            auto line = text((i == (size_t)state_.settingsSection() ? "▸ " : "  ") + sections[i]);
            if (i == (size_t)state_.settingsSection()) {
                line = line | bold;
                if (!settingsFocusContent_) {
                    line = line | color(th.accent);
                }
            }
            sectionList.push_back(line);
        }

        // Helper to highlight selected field
        auto fieldStyle = [this, &th](int fieldIdx, Element el) {
            if (settingsFocusContent_ && selectedSettingsItem_ == fieldIdx) {
                return el | bgcolor(th.primary) | color(th.primaryFg);
            }
            return el;
        };

        Elements content;
        switch (state_.settingsSection()) {
            case SettingsSection::Network: {
                content.push_back(text(" NETWORK") | bold);
                content.push_back(separator());

                // Field 0: Listen Port
                std::string portVal = settingsEditMode_ && selectedSettingsItem_ == 0 ?
                    settingsEditBuffer_ + "▏" : std::to_string(state_.listenPort());
                content.push_back(fieldStyle(0, hbox({
                    text("  Listen Port:      "),
                    text(portVal) | border
                })));

                content.push_back(text(""));
                content.push_back(text("  Bandwidth Limits") | dim);

                // Field 1: Limit upload checkbox + value
                {
                    std::string ulVal = settingsEditMode_ && selectedSettingsItem_ == 1 && state_.limitUpload() ?
                        settingsEditBuffer_ + "▏ KB/s" : std::to_string(state_.uploadLimitKBps()) + " KB/s";
                    content.push_back(fieldStyle(1, hbox({
                        text(state_.limitUpload() ? "  [x] " : "  [ ] "),
                        text("Limit upload    "),
                        text(ulVal) | border
                    })));
                }

                // Field 2: Limit download checkbox + value
                {
                    std::string dlVal = settingsEditMode_ && selectedSettingsItem_ == 2 && state_.limitDownload() ?
                        settingsEditBuffer_ + "▏ KB/s" : std::to_string(state_.downloadLimitKBps()) + " KB/s";
                    content.push_back(fieldStyle(2, hbox({
                        text(state_.limitDownload() ? "  [x] " : "  [ ] "),
                        text("Limit download  "),
                        text(dlVal) | border
                    })));
                }

                content.push_back(text(""));

                // Field 3: Accept incoming checkbox
                content.push_back(fieldStyle(3, hbox({
                    text(state_.acceptIncoming() ? "  [x] " : "  [ ] "),
                    text("Accept file requests")
                })));

                content.push_back(text(""));
                content.push_back(text("  Enter: edit, Space: toggle, s: save") | dim);
                break;
            }

            case SettingsSection::Sharing: {
                content = {text(" SHARING") | bold, separator()};
                content.push_back(text("  Shared Directories") | dim);
                content.push_back(text(""));

                if (state_.sharedDirs().empty()) {
                    content.push_back(text("  (no directories shared)") | dim);
                } else {
                    int idx = 0;
                    for (const auto& dir : state_.sharedDirs()) {
                        bool selected = settingsFocusContent_ && idx == selectedSettingsItem_;
                        auto line = hbox({
                            text(selected ? "  ▸ " : "    "),
                            text(dir.path) | (selected ? bold : nothing),
                            filler(),
                            text(std::to_string(dir.fileCount) + " files  "),
                            text(formatSize(dir.totalSize))
                        });
                        if (selected) {
                            line = line | bgcolor(th.primary) | color(th.primaryFg);
                        }
                        content.push_back(line);
                        idx++;
                    }
                }

                content.push_back(text(""));
                content.push_back(hbox({
                    text("  "),
                    text(" a:Add ") | border,
                    text(" "),
                    text(" d:Remove ") | border,
                    text(" "),
                    text(" r:Rescan ") | border
                }));
                break;
            }

            case SettingsSection::Identity: {
                content.push_back(text(" IDENTITY") | bold);
                content.push_back(separator());

                // Field 0: Nickname
                std::string nickVal = settingsEditMode_ && selectedSettingsItem_ == 0 ?
                    settingsEditBuffer_ + "▏" : state_.nickname();
                if (nickVal.empty()) nickVal = "(not set)";
                content.push_back(fieldStyle(0, hbox({
                    text("  Nickname:  "),
                    text(nickVal) | border
                })));

                // Field 1: Network Name
                std::string netVal = settingsEditMode_ && selectedSettingsItem_ == 1 ?
                    settingsEditBuffer_ + "▏" : state_.networkHash();
                if (netVal.empty()) netVal = "(open network)";
                content.push_back(fieldStyle(1, hbox({
                    text("  Network:   "),
                    text(netVal) | border
                })));

                content.push_back(text(""));
                content.push_back(text("  RSA Key") | dim);
                content.push_back(text("  ● Key loaded (2048-bit)") | color(th.success));
                content.push_back(text("  Fingerprint: 3A:F2:91:BC:44:D8:7E:...") | dim);
                content.push_back(text(""));
                content.push_back(hbox({
                    text("  "),
                    text(" g:Generate ") | border,
                    text(" "),
                    text(" i:Import ") | border,
                    text(" "),
                    text(" e:Export ") | border
                }));
                content.push_back(text(""));
                content.push_back(text("  →/l to edit, Enter to save") | dim);
                break;
            }

            case SettingsSection::Interface: {
                content = {
                    text(" INTERFACE") | bold,
                    separator(),
                    text("  Color Theme") | dim,
                    text(""),
                };

                const auto& themes = builtinThemes();
                Elements themeRows;
                for (int i = 0; i < (int)themes.size(); ++i) {
                    const auto& t = themes[i];
                    bool isActive = (i == state_.themeIndex());
                    bool isHighlighted = settingsFocusContent_ && (i == selectedSettingsItem_);

                    // Radio indicator + name
                    std::string indicator = isActive ? " ● " : " ○ ";
                    auto nameEl = text(indicator + t.name);
                    if (isActive) {
                        nameEl = nameEl | bold;
                    }

                    // Color swatch
                    auto swatch = hbox({
                        text("  ") | bgcolor(t.accent),
                        text(" "),
                        text("  ") | bgcolor(t.primary),
                        text(" "),
                        text("  ") | bgcolor(t.success),
                        text(" "),
                        text("  ") | bgcolor(t.warning),
                        text(" "),
                        text("  ") | bgcolor(t.error),
                        text(" "),
                        text("  ") | bgcolor(t.notification),
                    });

                    auto row = hbox({
                        nameEl | size(WIDTH, EQUAL, 20),
                        text(" "),
                        swatch,
                    });

                    if (isHighlighted) {
                        row = row | bgcolor(th.primary) | color(th.primaryFg);
                        if (!isActive) {
                            row = row | focus;  // scroll to highlighted item
                        }
                    }
                    if (isActive && !isHighlighted) {
                        row = row | color(th.accent);
                    }
                    if (isActive) {
                        row = row | focus;
                    }

                    themeRows.push_back(row);
                }

                content.push_back(
                    vbox(themeRows) | vscroll_indicator | yframe | flex
                );
                content.push_back(text(""));
                content.push_back(text("  Enter/Space: select theme") | dim);
                break;
            }
        }

        // Visual indicator of focus
        auto sectionBox = vbox(sectionList) | size(WIDTH, EQUAL, 15);
        auto contentBox = vbox(content) | flex;

        if (!settingsFocusContent_) {
            sectionBox = sectionBox | border | color(th.accent);
            contentBox = contentBox | border | dim;
        } else {
            sectionBox = sectionBox | border | dim;
            contentBox = contentBox | border | color(th.accent);
        }

        return hbox({sectionBox, contentBox}) | flex;
    });
    // Note: Settings event handling is done in handleGlobalEvent()
}

void App::updateModalFlags() {
    // Not used currently
}

Component App::buildUI() {
    auto networkView = buildNetworkView();
    auto searchView = buildSearchView();
    auto transfersView = buildTransfersView();
    auto chatView = buildChatView();
    auto browseView = buildBrowseView();
    auto keysView = buildKeysView();
    auto settingsView = buildSettingsView();

    auto tabs = Container::Tab({
        networkView,
        searchView,
        transfersView,
        chatView,
        keysView,
        settingsView
    }, &tabIndex_);

    // Use CatchEvent on tabs first to handle global events, then let tabs handle the rest
    auto mainComponent = CatchEvent(tabs, [this](Event event) {
        return handleGlobalEvent(event);
    });

    return Renderer(mainComponent, [=, this] {
        // Gather state info first, then release lock before rendering
        bool showBrowse = false;
        bool showModal = false;
        std::string modalType;
        std::string modalInfo;
        Color themeBg = Color::Default;
        Color themeFg = Color::Default;
        {
            std::lock_guard<std::mutex> lock(state_.mutex());
            const auto& th = state_.theme();
            themeBg = th.bg;
            themeFg = th.fg;
            showBrowse = (state_.currentView() == View::Browse);
            showModal = state_.showModal();
            if (showModal) {
                modalType = state_.modalType();
                if (modalType == "download_confirm") {
                    auto& results = state_.searchResults();
                    int idx = state_.selectedSearchIndex();
                    modalInfo = (idx >= 0 && idx < (int)results.size())
                        ? results[idx].filename : "file";
                }
            }
        }

        // Render main content (no lock held)
        Element mainContent = showBrowse ? browseView->Render() : tabs->Render();

        // Render help overlay if needed
        Element help = showHelp_ ? buildHelpOverlay() : emptyElement();

        // Render modal overlay if needed (simple text-based, no component)
        Element modalOverlay = emptyElement();
        if (showModal) {
            if (modalType == "add_connection") {
                int labelWidth = 10;
                auto field1 = text(modalAddressInput_.empty() ? " " : modalAddressInput_);
                auto field2 = text(modalPortInput_.empty() ? "4001" : modalPortInput_);
                if (modalFieldFocus_ == 0) field1 = field1 | inverted;
                else field2 = field2 | inverted;

                modalOverlay = window(text(" Add Connection "), vbox({
                    hbox({text("Address: ") | size(WIDTH, EQUAL, labelWidth), field1 | flex | border}),
                    hbox({text("Port: ") | size(WIDTH, EQUAL, labelWidth), field2 | flex | border}),
                    separator(),
                    hbox({filler(), text(" [Tab] Switch  [Enter] OK  [Esc] Cancel ") | dim, filler()})
                })) | size(WIDTH, GREATER_THAN, 45) | clear_under | center;
            } else if (modalType == "confirm_disconnect") {
                modalOverlay = window(text(" Disconnect "), vbox({
                    text("Disconnect from " + disconnectPeerName_ + "?") | center,
                    separator(),
                    hbox({filler(), text(" [y] Yes  [n/Esc] No ") | dim, filler()})
                })) | size(WIDTH, GREATER_THAN, 30) | clear_under | center;
            } else if (modalType == "join_room") {
                modalOverlay = window(text(" Join Room "), vbox({
                    hbox({text("Room: "), text(modalRoomInput_.empty() ? " " : modalRoomInput_) | inverted | flex | border}),
                    separator(),
                    hbox({filler(), text(" [Enter] OK  [Esc] Cancel ") | dim, filler()})
                })) | size(WIDTH, GREATER_THAN, 40) | clear_under | center;
            } else if (modalType == "start_dm") {
                modalOverlay = window(text(" Direct Message "), vbox({
                    hbox({text("Peer: "), text(modalDmPeerInput_.empty() ? " " : modalDmPeerInput_) | inverted | flex | border}),
                    separator(),
                    hbox({filler(), text(" [Enter] OK  [Esc] Cancel ") | dim, filler()})
                })) | size(WIDTH, GREATER_THAN, 40) | clear_under | center;
            } else if (modalType == "add_directory") {
                modalOverlay = window(text(" Add Directory "), vbox({
                    hbox({text("Path: "), text(modalPathInput_.empty() ? " " : modalPathInput_) | inverted | flex | border}),
                    separator(),
                    hbox({filler(), text(" [Enter] OK  [Esc] Cancel ") | dim, filler()})
                })) | size(WIDTH, GREATER_THAN, 40) | clear_under | center;
            } else if (modalType == "browse_directory") {
                // Build directory listing with focus on selected item for auto-scroll
                Elements dirList;
                for (int i = 0; i < (int)browserEntries_.size(); i++) {
                    bool selected = (i == browserSelectedIndex_);
                    std::string prefix = selected ? " ▸ " : "   ";
                    std::string icon = (browserEntries_[i] == "..") ? "📁 " : "📂 ";
                    auto entry = text(prefix + icon + browserEntries_[i]);
                    if (selected) {
                        entry = entry | inverted | focus;  // focus makes frame scroll to this
                    }
                    dirList.push_back(entry);
                }
                if (dirList.empty()) {
                    dirList.push_back(text("   (empty directory)") | dim);
                }

                modalOverlay = window(text(" Select Directory "), vbox({
                    text(" Current: " + browserCurrentPath_) | bold,
                    separator(),
                    vbox(dirList) | vscroll_indicator | yframe | size(HEIGHT, LESS_THAN, 15),
                    separator(),
                    hbox({
                        text(" ↑↓:Navigate ") | dim,
                        text(" Enter:Open ") | dim,
                        text(" Space/s:Select ") | dim,
                        text(" Bksp:Up ") | dim,
                        text(" Esc:Cancel ") | dim,
                    })
                })) | size(WIDTH, GREATER_THAN, 50) | clear_under | center;
            } else if (modalType == "download_confirm") {
                modalOverlay = window(text(" Download "), vbox({
                    text("Download " + modalInfo + "?") | center,
                    separator(),
                    hbox({filler(), text(" [y] Yes  [n/Esc] No ") | dim, filler()})
                })) | size(WIDTH, GREATER_THAN, 30) | clear_under | center;
            } else if (modalType == "export_key") {
                modalOverlay = window(text(" Export Public Key "), vbox({
                    text("Export your public key for sharing with peers.") | dim,
                    text(""),
                    hbox({text("File: "), text(modalPathInput_.empty() ? " " : modalPathInput_) | inverted | flex | border}),
                    separator(),
                    hbox({filler(), text(" [Enter] Export  [Esc] Cancel ") | dim, filler()})
                })) | size(WIDTH, GREATER_THAN, 55) | clear_under | center;
            } else if (modalType == "import_key") {
                modalOverlay = window(text(" Import Public Key "), vbox({
                    text("Import a peer's public key to allow connections.") | dim,
                    text(""),
                    hbox({text("File: "), text(modalPathInput_.empty() ? " " : modalPathInput_) | inverted | flex | border}),
                    separator(),
                    hbox({filler(), text(" [Enter] Import  [Esc] Cancel ") | dim, filler()})
                })) | size(WIDTH, GREATER_THAN, 55) | clear_under | center;
            }
        }

        // Build the main layout
        auto mainLayout = vbox({
            buildStatusBar(),
            buildTabBar(),
            mainContent | flex,
            buildFooter(),
        }) | bgcolor(themeBg) | color(themeFg);

        // Layer modal and help on top using dbox (depth box)
        return dbox({
            mainLayout,
            modalOverlay,
            help,
        });
    });
}

}  // namespace waste
