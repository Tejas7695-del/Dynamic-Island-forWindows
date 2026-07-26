@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
cl.exe /DUNICODE /D_UNICODE /std:c++20 /await /EHsc src\main.cpp src\OverlayWindow.cpp user32.lib d3d11.lib dxgi.lib d2d1.lib dcomp.lib ole32.lib dwrite.lib winhttp.lib Shcore.lib windowsapp.lib windowscodecs.lib shlwapi.lib gdi32.lib dwmapi.lib wbemuuid.lib /Fe:DynamicIsland.exe
