@echo off
setlocal
set "ROOT=%~dp0"
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cl /nologo /W4 /WX /EHsc /O2 /MT /std:c++20 /I"%ROOT%build\dlss-sdk-api\include" /Fe:"%ROOT%build\framegen_nr_host.exe" /Fo:"%ROOT%build\framegen_nr_host.obj" "%ROOT%tests\framegen_nr_host.cpp" /link d3d12.lib dxgi.lib user32.lib advapi32.lib shlwapi.lib "%ROOT%build\dlss-sdk-api\lib\Windows_x86_64\x64\nvsdk_ngx_s.lib" || exit /b 1
endlocal
