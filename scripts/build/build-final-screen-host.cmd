@echo off
setlocal
for %%I in ("%~dp0..\..") do set "ROOT=%%~fI\"
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /Fo"%ROOT%build\final_screen_host.obj" /Fe"%ROOT%build\final_screen_host.exe" "%ROOT%tests\final_screen_host.cpp" /link d3d12.lib dxgi.lib user32.lib || exit /b 1
