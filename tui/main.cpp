#include "app.h"
#include <iostream>
#include <csignal>
#include <cstring>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

namespace {
    waste::App* g_app = nullptr;
}

void signalHandler(int signal) {
    if (g_app) {
        g_app->quit();
    }
}

// Minimal FTXUI test to verify library works
void runMinimalTest() {
    using namespace ftxui;

    auto screen = ScreenInteractive::Fullscreen();

    auto component = Renderer([&] {
        return vbox({
            text("FTXUI Test - Press 'q' to quit") | bold | center,
            separator(),
            text("If you can see this, FTXUI is working!") | center,
            filler(),
            text("Press any key to test input") | dim | center,
        }) | border;
    }) | CatchEvent([&](Event event) {
        if (event == Event::Character('q')) {
            screen.Exit();
            return true;
        }
        return false;
    });

    screen.Loop(component);
}

// First-run setup dialog
bool runFirstRunSetup(waste::App& app) {
    using namespace ftxui;

    auto screen = ScreenInteractive::Fullscreen();
    int choice = 0;  // 0 = generate, 1 = import
    std::string importPath;
    bool done = false;
    bool success = false;
    std::string errorMsg;

    auto generateButton = Button(" Generate New Keys ", [&] {
        if (app.generateNewKeys()) {
            success = true;
            done = true;
            screen.Exit();
        } else {
            errorMsg = "Failed to generate keys";
        }
    });

    auto importInput = Input(&importPath, "Path to key file...");

    auto importButton = Button(" Import Key File ", [&] {
        if (importPath.empty()) {
            errorMsg = "Please enter a path";
            return;
        }
        if (app.importExistingKeys(importPath)) {
            success = true;
            done = true;
            screen.Exit();
        } else {
            errorMsg = "Failed to import key file";
        }
    });

    auto quitButton = Button(" Quit ", [&] {
        done = true;
        screen.Exit();
    });

    auto container = Container::Vertical({
        generateButton,
        importInput,
        importButton,
        quitButton,
    });

    auto renderer = Renderer(container, [&] {
        Elements content = {
            text("WASTE TUI - First Run Setup") | bold | center,
            separator(),
            text("") | center,
            text("No encryption keys found.") | center,
            text("Keys are required for secure P2P communication.") | center,
            text("") | center,
            separator(),
            text("") | center,
            text("Option 1: Generate new keys") | bold,
            generateButton->Render() | center,
            text("") | center,
            separator(),
            text("") | center,
            text("Option 2: Import existing key file") | bold,
            hbox({text("Path: "), importInput->Render() | flex}) | border,
            importButton->Render() | center,
            text("") | center,
        };

        if (!errorMsg.empty()) {
            content.push_back(separator());
            content.push_back(text(errorMsg) | color(Color::Red) | center);
        }

        content.push_back(filler());
        content.push_back(separator());
        content.push_back(quitButton->Render() | center);

        return vbox(content) | border | size(WIDTH, GREATER_THAN, 50) | center;
    });

    screen.Loop(renderer);
    return success;
}

int main(int argc, char* argv[]) {
    bool demoMode = false;

    // Check for command line arguments
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--test") == 0) {
            runMinimalTest();
            return 0;
        }
        if (std::strcmp(argv[i], "--demo") == 0) {
            demoMode = true;
        }
        if (std::strcmp(argv[i], "--version") == 0 || std::strcmp(argv[i], "-v") == 0) {
            std::cout << "waste-tui v1.8.1\n";
            return 0;
        }
        if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            std::cout << "WASTE TUI v1.8.1 - Terminal User Interface for WASTE P2P\n\n";
            std::cout << "Usage: waste-tui [options]\n\n";
            std::cout << "Options:\n";
            std::cout << "  -v, --version  Show version\n";
            std::cout << "  -h, --help     Show this help\n";
            std::cout << "  --demo         Run with demo data (no network)\n";
            std::cout << "  --test         Run minimal FTXUI test\n";
            return 0;
        }
    }

    // Set up signal handlers
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    try {
        waste::App app;
        g_app = &app;

        if (demoMode) {
            // Add demo data for testing
            std::cout << "Running in demo mode with simulation\n";

            // Start core in simulation mode (for testing callbacks)
            app.startSimulation();

            {
            auto& state = app.state();
            std::lock_guard<std::mutex> lock(state.mutex());

            // Demo peers
            waste::PeerInfo peer1;
            peer1.address = "192.168.1.5";
            peer1.port = 4001;
            peer1.nickname = "alice";
            peer1.status = waste::ConnectionStatus::Online;
            peer1.filesShared = 347;
            peer1.connectedAt = std::chrono::steady_clock::now() - std::chrono::hours(2);
            state.peers().push_back(peer1);

            waste::PeerInfo peer2;
            peer2.address = "10.0.0.22";
            peer2.port = 4001;
            peer2.nickname = "bob";
            peer2.status = waste::ConnectionStatus::Online;
            peer2.filesShared = 1204;
            peer2.connectedAt = std::chrono::steady_clock::now() - std::chrono::minutes(45);
            state.peers().push_back(peer2);

            waste::PeerInfo peer3;
            peer3.address = "192.168.1.50";
            peer3.port = 4001;
            peer3.status = waste::ConnectionStatus::Authenticating;
            state.peers().push_back(peer3);

            waste::PeerInfo peer4;
            peer4.address = "192.168.1.200";
            peer4.port = 4001;
            peer4.status = waste::ConnectionStatus::Failed;
            peer4.errorMsg = "(timeout)";
            state.peers().push_back(peer4);

            // Demo search results
            state.searchQuery() = "ambient music";
            state.searchResults().push_back({
                "Ambient_Compilation_2003.zip", 145 * 1024 * 1024,
                "zip", 3, "alice", "abc123"
            });
            state.searchResults().push_back({
                "ambient_loops_pack.rar", 52 * 1024 * 1024,
                "rar", 1, "bob", "def456"
            });
            state.searchResults().push_back({
                "Ambient Music - Sleep.mp3", 9 * 1024 * 1024,
                "mp3", 2, "charlie", "ghi789"
            });

            // Demo transfers
            waste::TransferInfo xfer1;
            xfer1.id = 1;
            xfer1.filename = "Ambient_Compilation_2003.zip";
            xfer1.direction = waste::TransferDirection::Download;
            xfer1.status = waste::TransferStatus::Active;
            xfer1.totalSize = 145 * 1024 * 1024;
            xfer1.transferred = 97 * 1024 * 1024;
            xfer1.speedKBps = 523;
            xfer1.peer = "alice";
            state.transfers().push_back(xfer1);

            waste::TransferInfo xfer2;
            xfer2.id = 2;
            xfer2.filename = "project_files.zip";
            xfer2.direction = waste::TransferDirection::Download;
            xfer2.status = waste::TransferStatus::Active;
            xfer2.totalSize = 50 * 1024 * 1024;
            xfer2.transferred = 14 * 1024 * 1024;
            xfer2.speedKBps = 312;
            xfer2.peer = "bob";
            state.transfers().push_back(xfer2);

            waste::TransferInfo xfer3;
            xfer3.id = 3;
            xfer3.filename = "my_video.mp4";
            xfer3.direction = waste::TransferDirection::Upload;
            xfer3.status = waste::TransferStatus::Active;
            xfer3.totalSize = 200 * 1024 * 1024;
            xfer3.transferred = 170 * 1024 * 1024;
            xfer3.speedKBps = 256;
            xfer3.peer = "bob";
            state.transfers().push_back(xfer3);

            // Demo chat messages
            auto& general = state.chatRooms()[0];
            auto now = std::chrono::system_clock::now();
            general.messages.push_back({
                "#general", "alice", "hey everyone",
                now - std::chrono::minutes(11), false
            });
            general.messages.push_back({
                "#general", "bob", "hi alice!",
                now - std::chrono::minutes(10), false
            });
            general.messages.push_back({
                "#general", "charlie", "anyone have that ambient album?",
                now - std::chrono::minutes(9), false
            });
            general.messages.push_back({
                "#general", "alice", "yeah I'm sharing it",
                now - std::chrono::minutes(8), false
            });
            general.messages.push_back({
                "#general", "alice", "search for \"ambient compilation\"",
                now - std::chrono::minutes(8), false
            });
            general.messages.push_back({
                "#general", "charlie", "found it, thanks!",
                now - std::chrono::minutes(6), false
            });
            general.messages.push_back({
                "#general", "* dave has joined", "",
                now - std::chrono::minutes(1), true
            });

            // Network stats
            state.setNetworkStats({4, 256.5f, 835.0f});

                // Demo shared directories
                state.sharedDirs().push_back({"~/Music", 1204, 4ULL * 1024 * 1024 * 1024, false});
                state.sharedDirs().push_back({"~/Documents/Share", 89, 120ULL * 1024 * 1024, false});
            }
        } else {
            // Normal mode - initialize core
            if (!app.initializeCore()) {
                if (app.needsFirstRunSetup()) {
                    // Show first-run setup dialog
                    if (!runFirstRunSetup(app)) {
                        std::cerr << "Setup cancelled or failed\n";
                        return 1;
                    }
                } else {
                    std::cerr << "Failed to initialize WASTE core\n";
                    return 1;
                }
            }
        }

        app.run();

        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
