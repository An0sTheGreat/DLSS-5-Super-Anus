@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /Fo"%~dp0build\final_screen_host.obj" /Fe"%~dp0build\final_screen_host.exe" "%~dp0tests\final_screen_host.cpp" /link d3d12.lib dxgi.lib user32.lib || exit /b 1
