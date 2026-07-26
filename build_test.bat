@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
cl.exe /std:c++20 /EHsc /await test_winrt.cpp windowsapp.lib
test_winrt.exe
