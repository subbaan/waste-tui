#ifndef WASTE_TUI_CORE_H
#define WASTE_TUI_CORE_H

#include "../state.h"
#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>

namespace waste {

// Forward declarations for WASTE core types
// These will be properly included in the .cpp file
struct WasteCoreImpl;

class WasteCore {
public:
    WasteCore();
    ~WasteCore();

    // Lifecycle
    enum class InitResult {
        Success,
        NoKeys,           // Keys don't exist, need first-run setup
        KeyLoadError,     // Keys exist but failed to load
        ListenError,      // Failed to bind listen port
        Error             // Other error
    };

    InitResult initialize(const std::string& configDir, int listenPort,
                          const std::string& networkName = "");
    void shutdown();
    bool isRunning() const { return running_; }

    // First-run key management
    bool generateKeys(const std::string& configDir);
    bool importKeys(const std::string& keyFilePath, const std::string& configDir);
    bool keysExist(const std::string& configDir) const;
    std::string getPublicKeyHash() const;

    // Key management
    std::vector<KeyInfo> getTrustedKeys() const;
    std::vector<KeyInfo> getPendingKeys() const;
    void trustPendingKey(int index);
    void removeKey(int index, bool isPending);

    // Key import/export
    bool exportPublicKey(const std::string& filepath) const;
    bool importPublicKey(const std::string& filepath);
    std::string getDefaultExportPath() const;

    // Peer info updates (called from message callbacks)
    void updatePeerNickname(const std::string& address, const std::string& nickname);

    // Connection management
    void connectToPeer(const std::string& address, int port);
    void disconnectPeer(int index);
    void retryConnection(int index);
    int getPeerCount() const;

    // File sharing
    void addSharedDirectory(const std::string& path);
    void removeSharedDirectory(int index);
    void rescanSharedDirectories();
    std::vector<std::string> getSharedDirectories() const;
    void search(const std::string& query);
    void cancelSearch();

    // Transfers
    void downloadFile(const std::string& hash, const std::string& peer);
    void pauseTransfer(int id);
    void resumeTransfer(int id);
    void cancelTransfer(int id);

    // Chat
    void sendChatMessage(const std::string& room, const std::string& message);
    void joinRoom(const std::string& room);
    void leaveRoom(const std::string& room);

    // Browse peer files
    void browsePeer(const std::string& peer, const std::string& path);

    // Configuration
    void setNickname(const std::string& nick);
    std::string getNickname() const;
    void setListenPort(int port);
    int getListenPort() const;
    void setNetworkName(const std::string& name);
    std::string getNetworkName() const;
    void setAcceptIncoming(bool accept);
    bool getAcceptIncoming() const;
    void setThemeName(const std::string& name);
    std::string getThemeName() const;
    void setThrottleUpload(bool enabled, int kbps);
    void setThrottleDownload(bool enabled, int kbps);
    bool getThrottleUploadEnabled() const;
    bool getThrottleDownloadEnabled() const;
    int getThrottleUploadKBps() const;
    int getThrottleDownloadKBps() const;

    // Config file persistence
    bool loadConfig(const std::string& configDir = "");
    bool saveConfig();

    // Callbacks - set these before calling initialize()
    // All callbacks are called from the core thread, use post() to update UI
    std::function<void(const PeerInfo&)> onPeerConnected;
    std::function<void(int index, ConnectionStatus status, const std::string& error)> onPeerStatusChanged;
    std::function<void(int index)> onPeerDisconnected;

    std::function<void(const SearchResult&)> onSearchResult;
    std::function<void()> onSearchComplete;

    std::function<void(const TransferInfo&)> onTransferAdded;
    std::function<void(int id, uint64_t transferred, uint64_t totalSize, float speedKBps)> onTransferProgress;
    std::function<void(int id, TransferStatus status, const std::string& error)> onTransferStatusChanged;

    std::function<void(const ChatMessage&)> onChatMessage;
    std::function<void(const std::string& room, const std::string& user, bool joined)> onUserPresence;

    std::function<void(const std::string& peer, const std::vector<BrowseEntry>&)> onBrowseResults;
    std::function<void(const std::string& address, const std::string& nickname)> onPeerNicknameChanged;

    std::function<void(const NetworkStats&)> onNetworkStatsUpdated;

private:
    // Background event loop
    void eventLoop();
    void processMessages();
    void processTransfers();
    void updateStats();
    void processSimulation(int64_t elapsedMs);
    void updatePeerListFromConnections();
    void rescanSharedDirectoriesInternal();

    // WASTE core state (pimpl pattern to hide WASTE headers from TUI)
    std::unique_ptr<WasteCoreImpl> impl_;

    std::thread eventThread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> shouldStop_{false};
    mutable std::mutex mutex_;

    // Configuration
    std::string configDir_;
    std::string nickname_;
    std::string networkName_;
    std::string themeName_ = "Default";
    int listenPort_{4001};

    // Prevent copying
    WasteCore(const WasteCore&) = delete;
    WasteCore& operator=(const WasteCore&) = delete;
};

}  // namespace waste

#endif  // WASTE_TUI_CORE_H
