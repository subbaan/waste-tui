#ifndef WASTE_TUI_STATE_H
#define WASTE_TUI_STATE_H

#include <string>
#include <vector>
#include <mutex>
#include <functional>
#include <chrono>

namespace waste {

// Connection status
enum class ConnectionStatus {
    Connecting,
    Authenticating,
    Online,
    Failed
};

// Peer connection info
struct PeerInfo {
    std::string address;
    int port;
    std::string nickname;
    ConnectionStatus status;
    int filesShared;
    std::chrono::steady_clock::time_point connectedAt;
    std::string errorMsg;  // For failed connections
};

// Search result
struct SearchResult {
    std::string filename;
    uint64_t size;
    std::string type;
    int sources;
    std::string user;
    std::string hash;
};

// Transfer info
enum class TransferStatus {
    Active,
    Paused,
    Queued,
    Completed,
    Failed
};

enum class TransferDirection {
    Download,
    Upload
};

struct TransferInfo {
    int id;
    std::string filename;
    TransferDirection direction;
    TransferStatus status;
    uint64_t totalSize;
    uint64_t transferred;
    float speedKBps;
    std::string peer;
    std::string errorMsg;
};

// Public key info for display
struct KeyInfo {
    std::string name;           // Peer nickname
    std::string fingerprint;    // SHA-1 hash as hex string
    int bits;                   // Key size (e.g., 2048)
    bool isPending;             // In pending list vs trusted list
};

// Chat message
struct ChatMessage {
    std::string room;
    std::string sender;
    std::string content;
    std::chrono::system_clock::time_point timestamp;
    bool isSystem;  // Join/leave messages
};

// Chat room
struct ChatRoom {
    std::string name;
    bool isDirect;  // Direct message vs channel
    int unreadCount;
    std::vector<ChatMessage> messages;
    std::vector<std::string> users;  // Users in channel (for presence tracking)
};

// File browser entry
struct BrowseEntry {
    std::string name;
    bool isDirectory;
    uint64_t size;
};

// Shared directory
struct SharedDirectory {
    std::string path;
    int fileCount;
    uint64_t totalSize;
    bool scanning;
};

// Current view
enum class View {
    Network,
    Search,
    Transfers,
    Chat,
    Keys,
    Browse,
    Settings
};

// Settings section (for Settings view)
enum class SettingsSection {
    Network,
    Sharing,
    Identity,
    Interface
};

// Network statistics
struct NetworkStats {
    int connectedPeers;
    float uploadKBps;
    float downloadKBps;
};

// Application state - thread-safe access
class AppState {
public:
    AppState();

    // Thread-safe state access
    std::mutex& mutex() { return mutex_; }

    // Current view
    View currentView() const { return currentView_; }
    void setCurrentView(View view) { currentView_ = view; }

    // Network stats
    NetworkStats networkStats() const { return networkStats_; }
    void setNetworkStats(const NetworkStats& stats) { networkStats_ = stats; }

    // Peers
    std::vector<PeerInfo>& peers() { return peers_; }
    const std::vector<PeerInfo>& peers() const { return peers_; }
    int selectedPeerIndex() const { return selectedPeerIndex_; }
    void setSelectedPeerIndex(int idx) { selectedPeerIndex_ = idx; }

    // Search
    std::string& searchQuery() { return searchQuery_; }
    std::vector<SearchResult>& searchResults() { return searchResults_; }
    int selectedSearchIndex() const { return selectedSearchIndex_; }
    void setSelectedSearchIndex(int idx) { selectedSearchIndex_ = idx; }

    // Transfers
    std::vector<TransferInfo>& transfers() { return transfers_; }
    int selectedTransferIndex() const { return selectedTransferIndex_; }
    void setSelectedTransferIndex(int idx) { selectedTransferIndex_ = idx; }

    // Chat
    std::vector<ChatRoom>& chatRooms() { return chatRooms_; }
    int selectedRoomIndex() const { return selectedRoomIndex_; }
    void setSelectedRoomIndex(int idx) { selectedRoomIndex_ = idx; }
    std::string& chatInput() { return chatInput_; }

    // Browse
    std::string browsePeer() const { return browsePeer_; }
    void setBrowsePeer(const std::string& peer) { browsePeer_ = peer; }
    std::string browsePath() const { return browsePath_; }
    void setBrowsePath(const std::string& path) { browsePath_ = path; }
    std::vector<BrowseEntry>& browseEntries() { return browseEntries_; }
    int selectedBrowseIndex() const { return selectedBrowseIndex_; }

    // Keys
    std::vector<KeyInfo>& trustedKeys() { return trustedKeys_; }
    std::vector<KeyInfo>& pendingKeys() { return pendingKeys_; }
    int selectedKeyIndex() const { return selectedKeyIndex_; }
    void setSelectedKeyIndex(int idx) { selectedKeyIndex_ = idx; }
    bool showPendingKeys() const { return showPendingKeys_; }
    void setShowPendingKeys(bool pending) { showPendingKeys_ = pending; }
    void setSelectedBrowseIndex(int idx) { selectedBrowseIndex_ = idx; }

    // Settings
    SettingsSection settingsSection() const { return settingsSection_; }
    void setSettingsSection(SettingsSection section) { settingsSection_ = section; }

    // Settings values
    int listenPort() const { return listenPort_; }
    void setListenPort(int port) { listenPort_ = port; }
    int maxConnections() const { return maxConnections_; }
    void setMaxConnections(int max) { maxConnections_ = max; }
    int uploadLimitKBps() const { return uploadLimitKBps_; }
    void setUploadLimitKBps(int limit) { uploadLimitKBps_ = limit; }
    int downloadLimitKBps() const { return downloadLimitKBps_; }
    void setDownloadLimitKBps(int limit) { downloadLimitKBps_ = limit; }
    bool limitUpload() const { return limitUpload_; }
    void setLimitUpload(bool limit) { limitUpload_ = limit; }
    bool limitDownload() const { return limitDownload_; }
    void setLimitDownload(bool limit) { limitDownload_ = limit; }
    std::string networkHash() const { return networkHash_; }
    void setNetworkHash(const std::string& hash) { networkHash_ = hash; }
    bool acceptIncoming() const { return acceptIncoming_; }
    void setAcceptIncoming(bool accept) { acceptIncoming_ = accept; }
    std::string nickname() const { return nickname_; }
    void setNickname(const std::string& nick) { nickname_ = nick; }
    std::vector<SharedDirectory>& sharedDirs() { return sharedDirs_; }

    // Modal state
    bool showModal() const { return showModal_; }
    void setShowModal(bool show) { showModal_ = show; }
    std::string modalType() const { return modalType_; }
    void setModalType(const std::string& type) { modalType_ = type; }

    // Previous view (for Browse back navigation)
    View previousView() const { return previousView_; }
    void setPreviousView(View view) { previousView_ = view; }

private:
    mutable std::mutex mutex_;

    View currentView_ = View::Network;
    View previousView_ = View::Network;
    NetworkStats networkStats_ = {0, 0.0f, 0.0f};

    // Peers
    std::vector<PeerInfo> peers_;
    int selectedPeerIndex_ = 0;

    // Search
    std::string searchQuery_;
    std::vector<SearchResult> searchResults_;
    int selectedSearchIndex_ = 0;

    // Transfers
    std::vector<TransferInfo> transfers_;
    int selectedTransferIndex_ = 0;

    // Chat
    std::vector<ChatRoom> chatRooms_;
    int selectedRoomIndex_ = 0;
    std::string chatInput_;

    // Browse
    std::string browsePeer_;
    std::string browsePath_ = "/";
    std::vector<BrowseEntry> browseEntries_;
    int selectedBrowseIndex_ = 0;

    // Settings
    SettingsSection settingsSection_ = SettingsSection::Network;
    int listenPort_ = 4001;
    int maxConnections_ = 32;
    int uploadLimitKBps_ = 128;
    int downloadLimitKBps_ = 0;
    bool limitUpload_ = true;
    bool limitDownload_ = false;
    std::string networkHash_;
    bool acceptIncoming_ = true;
    std::string nickname_ = "anonymous";
    std::vector<SharedDirectory> sharedDirs_;

    // Keys
    std::vector<KeyInfo> trustedKeys_;
    std::vector<KeyInfo> pendingKeys_;
    int selectedKeyIndex_ = 0;
    bool showPendingKeys_ = false;  // false = trusted, true = pending

    // Modal
    bool showModal_ = false;
    std::string modalType_;
};

}  // namespace waste

#endif  // WASTE_TUI_STATE_H
