#ifndef WASTE_TUI_APP_H
#define WASTE_TUI_APP_H

#include "state.h"
#include "core/waste_core.h"
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <memory>
#include <functional>
#include <atomic>
#include <thread>

namespace waste {

class App {
public:
    App();
    ~App();

    // Initialize the WASTE core (call before run())
    // Returns false if first-run setup is needed
    bool initializeCore();

    // Start core in simulation mode (for demo/testing without keys)
    void startSimulation();

    // First-run key setup
    bool needsFirstRunSetup() const { return needsFirstRun_; }
    bool generateNewKeys();
    bool importExistingKeys(const std::string& path);

    // Get core for direct access if needed
    WasteCore& core() { return *core_; }

    // Run the application (blocking)
    void run();

    // Thread-safe: request UI refresh
    void refresh();

    // Thread-safe: post callback to UI thread
    void post(std::function<void()> callback);

    // Get application state (lock mutex before accessing)
    AppState& state() { return state_; }

    // Quit the application
    void quit();

private:
    // Build the main UI component
    ftxui::Component buildUI();

    // Build individual views
    ftxui::Component buildNetworkView();
    ftxui::Component buildSearchView();
    ftxui::Component buildTransfersView();
    ftxui::Component buildChatView();
    ftxui::Component buildBrowseView();
    ftxui::Component buildKeysView();
    ftxui::Component buildSettingsView();

    // Build status bar
    ftxui::Element buildStatusBar();

    // Build tab bar
    ftxui::Element buildTabBar();

    // Build footer (keybinding hints)
    ftxui::Element buildFooter();

    // Build help overlay
    ftxui::Element buildHelpOverlay();

    // Handle global key events
    bool handleGlobalEvent(ftxui::Event event);

    // Modal builders
    ftxui::Component buildAddConnectionModal();
    ftxui::Component buildConfirmDisconnectModal();
    ftxui::Component buildJoinRoomModal();
    ftxui::Component buildAddDirectoryModal();
    ftxui::Component buildDownloadConfirmModal();

    AppState state_;
    ftxui::ScreenInteractive screen_;
    std::atomic<bool> running_{true};

    // Modal input state
    std::string modalAddressInput_;
    std::string modalPortInput_;
    std::string modalRoomInput_;
    std::string modalPathInput_;

    // For confirm disconnect modal
    std::string disconnectPeerName_;

    // For folder browser modal
    std::string browserCurrentPath_;
    std::vector<std::string> browserEntries_;
    int browserSelectedIndex_ = 0;

    // View-specific state
    int tabIndex_ = 0;
    int modalTabIndex_ = 0;  // 0 = no modal, 1-5 = specific modals
    int modalFieldFocus_ = 0; // For two-field modals: 0 = first field, 1 = second
    int selectedSettingsItem_ = 0;
    bool settingsFocusContent_ = false;  // false = section list, true = content area
    bool settingsEditMode_ = false;      // true when editing a text field
    std::string settingsEditBuffer_;     // Buffer for editing text fields

    // Help overlay
    bool showHelp_ = false;

    // Modal visibility flags (used by FTXUI Modal)
    bool showAddConnectionModal_ = false;
    bool showConfirmDisconnectModal_ = false;
    bool showJoinRoomModal_ = false;
    bool showAddDirectoryModal_ = false;
    bool showDownloadConfirmModal_ = false;

    // Update modal flags from state
    void updateModalFlags();

    // Folder browser helpers
    void refreshBrowserEntries();

    // Initialize core callbacks
    void setupCoreCallbacks();

    // For thread-safe refresh
    std::atomic<bool> needsRefresh_{false};

    // WASTE core integration
    std::unique_ptr<WasteCore> core_;
    std::string configDir_;
    bool needsFirstRun_{false};
};

}  // namespace waste

#endif  // WASTE_TUI_APP_H
