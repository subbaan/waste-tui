#include "state.h"

namespace waste {

AppState::AppState() {
    // Initialize with a default chat room
    ChatRoom general;
    general.name = "#general";
    general.isDirect = false;
    general.unreadCount = 0;
    chatRooms_.push_back(general);
}

}  // namespace waste
