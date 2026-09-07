@echo off
setlocal
for %%I in ("%~dp0..\..") do set "ROOT=%%~fI\"
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cl /nologo /W4 /WX /EHsc /O2 /MT /std:c++17 /DNGXGYM_FFXIV_FIXTURE /I"%ROOT%build\dlss-sdk-api\include" ^
 /Fe:"%ROOT%build\ngxGym-ffxiv.exe" /Fo:"%ROOT%build\ngxGym-ffxiv.obj" "%ROOT%build\ngx-test-host\src\d3d11.cpp" ^
 /link d3d11.lib dxgi.lib d3dcompiler.lib user32.lib advapi32.lib shlwapi.lib ^
 /EXPORT:NVSDK_NGX_D3D11_CreateFeature /EXPORT:NVSDK_NGX_D3D11_ReleaseFeature ^
 /EXPORT:NVSDK_NGX_D3D11_EvaluateFeature /EXPORT:NVSDK_NGX_D3D11_EvaluateFeature_C /EXPORT:NVSDK_NGX_D3D11_Shutdown1 ^
 "%ROOT%build\dlss-sdk-api\lib\Windows_x86_64\x64\nvsdk_ngx_s.lib" || exit /b 1
endlocal
