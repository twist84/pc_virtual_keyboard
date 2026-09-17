cmake_minimum_required(VERSION 3.20)
project(pc_xbox360_ui LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(COMMON_LIBS user32 gdi32 msimg32 xinput shell32)

# ── Shared static library ─────────────────────────────────────────
add_library(xbox360_ui_common STATIC
    xbox360_ui_common.cpp
    controller_message_box.cpp
    config.cpp
)
target_include_directories(xbox360_ui_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(xbox360_ui_common PUBLIC UNICODE _UNICODE)
target_link_libraries(xbox360_ui_common PUBLIC ${COMMON_LIBS})

# Helper macro for Guide panel executables
macro(add_guide_app name source)
    add_executable(${name} WIN32 ${source})
    target_compile_features(${name} PRIVATE cxx_std_17)
    target_compile_definitions(${name} PRIVATE UNICODE _UNICODE ${ARGN})
    target_link_libraries(${name} PRIVATE xbox360_ui_common)
endmacro()

# Standalone panel demos
add_guide_app(pc_friends_list      friends_list.cpp)
add_guide_app(pc_quick_launch      quick_launch.cpp)
add_guide_app(pc_gamercard         gamercard.cpp)
add_guide_app(pc_party             party.cpp)
add_guide_app(pc_players_met       players_met.cpp)
add_guide_app(pc_message_center    message_center.cpp)

# Virtual keyboard (standalone has its own WinMain)
add_guide_app(pc_virtual_keyboard  virtual_keyboard_overlapped.cpp)

# Unified Guide embeds keyboard without WinMain
add_executable(pc_guide WIN32 guide.cpp virtual_keyboard_overlapped.cpp)
target_compile_features(pc_guide PRIVATE cxx_std_17)
target_compile_definitions(pc_guide PRIVATE UNICODE _UNICODE VIRTUAL_KEYBOARD_NO_MAIN)
target_link_libraries(pc_guide PRIVATE xbox360_ui_common)

# Background listener: Guide button / Start+Back opens pc_guide.exe
add_executable(pc_guide_listener WIN32 guide_listener.cpp)
target_compile_features(pc_guide_listener PRIVATE cxx_std_17)
target_compile_definitions(pc_guide_listener PRIVATE UNICODE _UNICODE)
target_link_libraries(pc_guide_listener PRIVATE user32 xinput shell32)

# Ship default config files next to Release/Debug outputs
file(GLOB APP_CONFIGS "${CMAKE_CURRENT_SOURCE_DIR}/config/*.ini")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/Release")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/Debug")
file(COPY ${APP_CONFIGS} DESTINATION "${CMAKE_BINARY_DIR}/Release")
file(COPY ${APP_CONFIGS} DESTINATION "${CMAKE_BINARY_DIR}/Debug")
