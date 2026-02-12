#include "state.h"
#include <set>
#include <algorithm>

namespace waste {

AppState::AppState() {
    // Initialize with a default chat room
    ChatRoom general;
    general.name = "#general";
    general.isDirect = false;
    general.unreadCount = 0;
    chatRooms_.push_back(general);
}

void AppState::rebuildBrowseEntries() {
    browseEntries_.clear();

    // Compute the prefix to match against full paths
    std::string prefix;
    if (browsePath_ == "/" || browsePath_.empty()) {
        prefix = "";
    } else {
        // browsePath_ is like "/Music" or "/Music/Albums"
        prefix = browsePath_.substr(1) + "/";  // "Music/" or "Music/Albums/"
    }

    std::set<std::string> seenDirs;

    for (const auto& raw : rawBrowseEntries_) {
        const std::string& path = raw.fullPath;

        // Check if path starts with prefix
        if (!prefix.empty()) {
            if (path.size() <= prefix.size() || path.substr(0, prefix.size()) != prefix) {
                continue;
            }
        }

        // Get the remaining path after prefix
        std::string remaining = prefix.empty() ? path : path.substr(prefix.size());
        if (remaining.empty()) continue;

        // Find next slash
        size_t slashPos = remaining.find('/');

        if (slashPos == std::string::npos) {
            // File at current level
            BrowseEntry entry;
            entry.name = remaining;
            entry.fullPath = raw.fullPath;
            entry.isDirectory = false;
            entry.size = raw.size;
            entry.fileId = raw.fileId;
            browseEntries_.push_back(entry);
        } else {
            // Subdirectory - add virtual directory entry (deduplicated)
            std::string dirName = remaining.substr(0, slashPos);
            if (!dirName.empty() && seenDirs.find(dirName) == seenDirs.end()) {
                seenDirs.insert(dirName);
                BrowseEntry entry;
                entry.name = dirName;
                entry.isDirectory = true;
                entry.size = 0;
                entry.fileId = -1;
                browseEntries_.push_back(entry);
            }
        }
    }

    // Sort: directories first, then files alphabetically
    std::sort(browseEntries_.begin(), browseEntries_.end(),
        [](const BrowseEntry& a, const BrowseEntry& b) {
            if (a.isDirectory != b.isDirectory) return a.isDirectory > b.isDirectory;
            return a.name < b.name;
        });
}

}  // namespace waste
