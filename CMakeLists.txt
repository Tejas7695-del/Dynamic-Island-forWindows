cmake_minimum_required(VERSION 3.15)
project(DynamicIsland)

set(CMAKE_CXX_STANDARD 20)

add_executable(DynamicIsland WIN32 
    src/main.cpp 
    src/OverlayWindow.cpp 
    src/OverlayWindow.h
)

target_link_libraries(DynamicIsland 
    d2d1 
    dwrite 
    d3d11
    dxgi
    dcomp
    windowscodecs 
    user32 
    gdi32 
    comctl32 
    dwmapi
    winhttp
)
