cmake_minimum_required(VERSION 3.20)
project(pc_virtual_keyboard LANGUAGES CXX)

add_executable(pc_virtual_keyboard WIN32
    virtual_keyboard_overlapped.cpp
)

target_compile_features(pc_virtual_keyboard PRIVATE cxx_std_17)
target_compile_definitions(pc_virtual_keyboard PRIVATE UNICODE _UNICODE)
target_link_libraries(pc_virtual_keyboard PRIVATE user32 gdi32 msimg32 xinput)
