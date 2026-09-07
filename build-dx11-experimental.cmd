@echo off
setlocal
set "ROOT=%~dp0"
set "NR_OUTPUT=%ROOT%build\dx11-experimental.addon64"
set "NR_PROBE_DEFINE="
set "NR_VALIDATION_FLAGS="
set "NR_TEST_DEFINE="
if /i "%~1"=="game-test" (
 set "NR_OUTPUT=%ROOT%build\dx11-integrated-game-test.addon64"
 set "NR_PROBE_DEFINE=/DNR_DX11_GAME_TEST"
 set "NR_VALIDATION_FLAGS=--dx11-game-test"
 set "NR_TEST_DEFINE=/DNR_DX11_GAME_TEST"
)
if /i "%~1"=="recycle-probe" (
 set "NR_OUTPUT=%ROOT%build\dx11-recycle-probe.addon64"
 set "NR_PROBE_DEFINE=/DNR_DX11_RECYCLE_PROBE"
)
if /i "%~1"=="replacement-probe" (
 set "NR_OUTPUT=%ROOT%build\dx11-replacement-probe.addon64"
 set "NR_PROBE_DEFINE=/DNR_DX11_REPLACEMENT_PROBE"
)
if /i "%~1"=="core-shutdown-probe" (
 set "NR_OUTPUT=%ROOT%build\dx11-core-shutdown-probe.addon64"
 set "NR_PROBE_DEFINE=/DNR_DX11_CORE_SHUTDOWN_PROBE"
)
if /i "%~1"=="core-recreate-probe" (
 set "NR_OUTPUT=%ROOT%build\dx11-core-recreate-probe.addon64"
 set "NR_PROBE_DEFINE=/DNR_DX11_CORE_SHUTDOWN_PROBE /DNR_DX11_CORE_RECREATE_PROBE"
)
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
"C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\dxc.exe" /nologo /T cs_6_0 /E Resample /Fo "%ROOT%build\neural_resample_v66.cso" "%ROOT%src\neural_resample.hlsl" || exit /b 1
python "%ROOT%tools\binary_to_header.py" "%ROOT%build\neural_resample_v66.cso" "%ROOT%src\neural_resample_shader.hpp" g_neural_resample_shader || exit /b 1
"C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\fxc.exe" /nologo /T cs_5_0 /E main /O3 /Fo "%ROOT%build\screenshot_copy.cso" "%ROOT%src\screenshot_copy.hlsl" || exit /b 1
python "%ROOT%tools\binary_to_header.py" "%ROOT%build\screenshot_copy.cso" "%ROOT%src\screenshot_copy_shader.hpp" g_screenshot_copy_shader || exit /b 1
"C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\fxc.exe" /nologo /T cs_5_0 /E main /O3 /Fo "%ROOT%build\dx11_depth_convert.cso" "%ROOT%src\dx11_depth_convert.hlsl" || exit /b 1
python "%ROOT%tools\binary_to_header.py" "%ROOT%build\dx11_depth_convert.cso" "%ROOT%src\dx11_depth_convert_shader.hpp" g_dx11_depth_convert_shader || exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /Fo"%ROOT%build\scale_history.obj" /Fe"%ROOT%build\scale_history.exe" "%ROOT%tests\scale_history.cpp" || exit /b 1
"%ROOT%build\scale_history.exe" || exit /b 1
cl /nologo /c /std:c++20 /MD /EHs-c- /O2 /Oi /GS- /GR- /guard:cf- /Zl /Brepro /W4 /WX /DNR_EXPERIMENTAL_DX11 %NR_PROBE_DEFINE% ^
 /I "%ROOT%build\minhook-api\include" /I "%ROOT%build\dlss-sdk-api\include" /I "C:\tmp\reshade-source\include" /I "C:\tmp\imgui-source" ^
 /Fo"%ROOT%build\neural_resolution_dx11.obj" "%ROOT%src\neural_resolution_addon.cpp" || exit /b 1
ml64 /nologo /c /Fo"%ROOT%build\embedded_bridges_dx11.obj" "%ROOT%src\embedded_bridges.asm" || exit /b 1
cl /nologo /c /MD /O2 /GS- /Zl /Brepro /Fo"%ROOT%build\\" "%ROOT%build\minhook-api\src\hook.c" "%ROOT%build\minhook-api\src\buffer.c" "%ROOT%build\minhook-api\src\trampoline.c" "%ROOT%build\minhook-api\src\hde\hde64.c" || exit /b 1
for %%T in (dx11_preinit_rollback dx11_native_registry dx11_replacement_session dx11_core_shutdown) do (
cl /nologo /std:c++20 /MD /EHsc /O2 /W4 /WX %NR_TEST_DEFINE% ^
 /I "%ROOT%build\minhook-api\include" /I "%ROOT%build\dlss-sdk-api\include" /I "C:\tmp\reshade-source\include" /I "C:\tmp\imgui-source" ^
 /Fo"%ROOT%build\%%T.obj" /Fe"%ROOT%build\%%T.exe" "%ROOT%tests\%%T.cpp" ^
 "%ROOT%build\hook.obj" "%ROOT%build\buffer.obj" "%ROOT%build\trampoline.obj" "%ROOT%build\hde64.obj" ^
 /link d3d11.lib d3d12.lib dxgi.lib user32.lib Psapi.lib ole32.lib windowscodecs.lib uuid.lib || exit /b 1
"%ROOT%build\%%T.exe" || exit /b 1
)
link /nologo /dll /nodefaultlib /entry:combined_entry /dynamicbase /incremental:no /Brepro /opt:ref /opt:icf ^
 /map:"%ROOT%build\neural_resolution_dx11.map" /out:"%ROOT%build\neural_resolution_dx11_embedded.dll" ^
 "%ROOT%build\neural_resolution_dx11.obj" "%ROOT%build\embedded_bridges_dx11.obj" "%ROOT%build\hook.obj" "%ROOT%build\buffer.obj" "%ROOT%build\trampoline.obj" "%ROOT%build\hde64.obj" kernel32.lib Psapi.lib User32.lib ucrt.lib ole32.lib windowscodecs.lib uuid.lib || exit /b 1
python "%ROOT%tools\patch_v6_addon.py" --base "%ROOT%updated-official-renodx-dlss.addon64" ^
 --embedded "%ROOT%build\neural_resolution_dx11_embedded.dll" --map "%ROOT%build\neural_resolution_dx11.map" ^
 --section-name .nr-dx11 --screenshot-capture --output "%NR_OUTPUT%" || exit /b 1
python "%ROOT%tools\validate_v6_addon.py" --base "%ROOT%updated-official-renodx-dlss.addon64" ^
 --addon "%NR_OUTPUT%" --version V6.6 --experimental-dx11 --screenshot-capture %NR_VALIDATION_FLAGS% || exit /b 1
copy /y "%ROOT%build\minhook-api\LICENSE.txt" "%ROOT%build\dx11-experimental-minhook-LICENSE.txt" >nul || exit /b 1
endlocal
