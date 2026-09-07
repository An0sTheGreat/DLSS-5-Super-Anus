@echo off
setlocal
for %%I in ("%~dp0..\..") do set "ROOT=%%~fI\"
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cl /nologo /W4 /EHsc /O2 /MT /std:c++17 /I"%ROOT%build\dlss-sdk-api\include" ^
 /Fe:"%ROOT%build\ngxGym.exe" /Fo:"%ROOT%build\ngxGym.obj" "%ROOT%build\ngx-test-host\src\d3d11.cpp" ^
 /link d3d11.lib dxgi.lib d3dcompiler.lib user32.lib advapi32.lib shlwapi.lib ^
 "%ROOT%build\dlss-sdk-api\lib\Windows_x86_64\x64\nvsdk_ngx_s.lib" || exit /b 1
endlocal
