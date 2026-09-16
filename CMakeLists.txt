cmake_minimum_required(VERSION 3.20)
project(pc_xbox360_ui LANGUAGES CXX)

set(COMMON_LIBS user32 gdi32 msimg32 xinput)

# Individual Guide panels (standalone demos)
add_executable(pc_virtual_keyboard WIN32 virtual_keyboard_overlapped.cpp)
target_compile_features(pc_virtual_keyboard PRIVATE cxx_std_17)
target_compile_definitions(pc_virtual_keyboard PRIVATE UNICODE _UNICODE)
target_link_libraries(pc_virtual_keyboard PRIVATE ${COMMON_LIBS})

add_executable(pc_friends_list WIN32 friends_list.cpp)
target_compile_features(pc_friends_list PRIVATE cxx_std_17)
target_compile_definitions(pc_friends_list PRIVATE UNICODE _UNICODE)
target_link_libraries(pc_friends_list PRIVATE ${COMMON_LIBS})

add_executable(pc_quick_launch WIN32 quick_launch.cpp)
target_compile_features(pc_quick_launch PRIVATE cxx_std_17)
target_compile_definitions(pc_quick_launch PRIVATE UNICODE _UNICODE)
target_link_libraries(pc_quick_launch PRIVATE ${COMMON_LIBS})

add_executable(pc_gamercard WIN32 gamercard.cpp)
target_compile_features(pc_gamercard PRIVATE cxx_std_17)
target_compile_definitions(pc_gamercard PRIVATE UNICODE _UNICODE)
target_link_libraries(pc_gamercard PRIVATE ${COMMON_LIBS})

add_executable(pc_party WIN32 party.cpp)
target_compile_features(pc_party PRIVATE cxx_std_17)
target_compile_definitions(pc_party PRIVATE UNICODE _UNICODE)
target_link_libraries(pc_party PRIVATE ${COMMON_LIBS})

add_executable(pc_players_met WIN32 players_met.cpp)
target_compile_features(pc_players_met PRIVATE cxx_std_17)
target_compile_definitions(pc_players_met PRIVATE UNICODE _UNICODE)
target_link_libraries(pc_players_met PRIVATE ${COMMON_LIBS})

add_executable(pc_message_center WIN32 message_center.cpp)
target_compile_features(pc_message_center PRIVATE cxx_std_17)
target_compile_definitions(pc_message_center PRIVATE UNICODE _UNICODE)
target_link_libraries(pc_message_center PRIVATE ${COMMON_LIBS})

# Unified Guide (all sections stitched together + virtual keyboard for message reply)
add_executable(pc_guide WIN32 guide.cpp virtual_keyboard_overlapped.cpp)
target_compile_features(pc_guide PRIVATE cxx_std_17)
target_compile_definitions(pc_guide PRIVATE UNICODE _UNICODE VIRTUAL_KEYBOARD_NO_MAIN)
target_link_libraries(pc_guide PRIVATE ${COMMON_LIBS})
