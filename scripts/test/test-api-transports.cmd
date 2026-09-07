@echo off
setlocal
for %%I in ("%~dp0..\..") do set "ROOT=%%~fI\"
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /Zi /Od /Fd"%ROOT%build\dx11_transport_integration.pdb" /Fo"%ROOT%build\dx11_transport_integration.obj" /Fe"%ROOT%build\dx11_transport_integration.exe" "%ROOT%tests\dx11_transport_integration.cpp" /link d3d11.lib d3d12.lib dxgi.lib || exit /b 1
"%ROOT%build\dx11_transport_integration.exe" %* || exit /b 1
cl /nologo /c /std:c++20 /MD /EHs-c- /O2 /Oi /GS- /GR- /guard:cf- /Zl /Brepro /W4 /WX /Fo"%ROOT%build\dx11_transport_embedding.obj" "%ROOT%tests\dx11_transport_embedding.cpp" || exit /b 1
if exist "%ROOT%build\neural_resolution_v66.obj" (
  link /nologo /dll /nodefaultlib /entry:combined_entry /dynamicbase /incremental:no /Brepro /opt:ref /opt:icf /map:"%ROOT%build\api_transport_link_smoke.map" /out:"%ROOT%build\api_transport_link_smoke.dll" "%ROOT%build\neural_resolution_v66.obj" "%ROOT%build\embedded_bridges_v66.obj" "%ROOT%build\dx11_transport_embedding.obj" kernel32.lib Psapi.lib User32.lib ucrt.lib || exit /b 1
)
endlocal
