cmake_minimum_required(VERSION 3.14)
project(waste-tui
    VERSION 1.10.0
    DESCRIPTION "WASTE P2P Client - TUI Interface"
    LANGUAGES CXX C
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# WASTE core directory
set(WASTE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

# Fetch FTXUI
include(FetchContent)

FetchContent_Declare(ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI
    GIT_TAG v5.0.0
)

FetchContent_GetProperties(ftxui)
if(NOT ftxui_POPULATED)
    FetchContent_Populate(ftxui)
    add_subdirectory(${ftxui_SOURCE_DIR} ${ftxui_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()

# WASTE core sources (C files)
set(WASTE_C_SOURCES
    ${WASTE_ROOT}/blowfish.c
    ${WASTE_ROOT}/rsa/nn.c
    ${WASTE_ROOT}/rsa/md5c.c
    ${WASTE_ROOT}/rsa/r_random.c
    ${WASTE_ROOT}/rsa/prime.c
    ${WASTE_ROOT}/rsa/rsa.c
    ${WASTE_ROOT}/rsa/r_keygen.c
)

# WASTE core sources (C++ files)
set(WASTE_CXX_SOURCES
    ${WASTE_ROOT}/asyncdns.cpp
    ${WASTE_ROOT}/config.cpp
    ${WASTE_ROOT}/connection.cpp
    ${WASTE_ROOT}/filedb.cpp
    ${WASTE_ROOT}/listen.cpp
    ${WASTE_ROOT}/mqueue.cpp
    ${WASTE_ROOT}/mqueuelist.cpp
    ${WASTE_ROOT}/netkern.cpp
    ${WASTE_ROOT}/sha.cpp
    ${WASTE_ROOT}/util.cpp
    ${WASTE_ROOT}/xfers.cpp
    ${WASTE_ROOT}/m_chat.cpp
    ${WASTE_ROOT}/m_file.cpp
    ${WASTE_ROOT}/m_keydist.cpp
    ${WASTE_ROOT}/m_lcaps.cpp
    ${WASTE_ROOT}/m_ping.cpp
    ${WASTE_ROOT}/m_search.cpp
    ${WASTE_ROOT}/m_upload.cpp
)

# TUI executable
add_executable(waste-tui
    main.cpp
    app.cpp
    state.cpp
    core/waste_core.cpp
    components/table.cpp
    components/scrolltext.cpp
    components/modal.cpp
    views/network.cpp
    views/search.cpp
    views/transfers.cpp
    views/chat.cpp
    views/browse.cpp
    views/settings.cpp
    ${WASTE_C_SOURCES}
    ${WASTE_CXX_SOURCES}
)

target_include_directories(waste-tui PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/components
    ${CMAKE_CURRENT_SOURCE_DIR}/views
    ${WASTE_ROOT}
    ${WASTE_ROOT}/rsa
)

target_link_libraries(waste-tui PRIVATE
    ftxui::screen
    ftxui::dom
    ftxui::component
)

# Platform-specific settings
if(UNIX AND NOT APPLE)
    target_link_libraries(waste-tui PRIVATE pthread)
endif()

# Install target
install(TARGETS waste-tui DESTINATION bin)

# Post-build message
add_custom_command(TARGET waste-tui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND ${CMAKE_COMMAND} -E echo "Build complete: ${CMAKE_CURRENT_BINARY_DIR}/waste-tui"
    COMMAND ${CMAKE_COMMAND} -E echo "Run with: ./waste-tui"
    COMMAND ${CMAKE_COMMAND} -E echo ""
)
