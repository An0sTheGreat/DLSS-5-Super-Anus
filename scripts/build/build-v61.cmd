@echo off
setlocal
for %%I in ("%~dp0..\..") do set "ROOT=%%~fI\"
set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
set "DXC=C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\dxc.exe"

call "%VCVARS%" >nul || exit /b 1
if not exist "%ROOT%build" mkdir "%ROOT%build"
if not exist "%ROOT%release" mkdir "%ROOT%release"

"%DXC%" /nologo /T cs_6_0 /E Resample /Fo "%ROOT%build\neural_resample_v61.cso" "%ROOT%src\neural_resample.hlsl" || exit /b 1
python "%ROOT%tools\binary_to_header.py" "%ROOT%build\neural_resample_v61.cso" "%ROOT%src\neural_resample_shader.hpp" g_neural_resample_shader || exit /b 1

cl /nologo /c /std:c++20 /EHs-c- /O2 /Oi /GS- /GR- /guard:cf- /Zl /Brepro /W4 ^
  /I "C:\tmp\reshade-source\include" /I "C:\tmp\imgui-source" ^
  /Fo"%ROOT%build\neural_resolution_v61.obj" "%ROOT%src\neural_resolution_addon.cpp" || exit /b 1
ml64 /nologo /c /Fo"%ROOT%build\embedded_bridges_v61.obj" "%ROOT%src\embedded_bridges.asm" || exit /b 1

link /nologo /dll /nodefaultlib /entry:combined_entry /dynamicbase /incremental:no /Brepro /opt:ref /opt:icf ^
  /map:"%ROOT%build\neural_resolution_v61.map" ^
  /out:"%ROOT%build\neural_resolution_v61_embedded.dll" ^
  "%ROOT%build\neural_resolution_v61.obj" "%ROOT%build\embedded_bridges_v61.obj" ^
  kernel32.lib Psapi.lib User32.lib || exit /b 1

python "%ROOT%tools\patch_v6_addon.py" ^
  --base "%ROOT%updated-official-renodx-dlss.addon64" ^
  --embedded "%ROOT%build\neural_resolution_v61_embedded.dll" ^
  --map "%ROOT%build\neural_resolution_v61.map" ^
  --output "%ROOT%release\renodx-dlss5Super-unified-neural-resolution-v6.1.addon64" || exit /b 1
python "%ROOT%tools\validate_v6_addon.py" ^
  --base "%ROOT%updated-official-renodx-dlss.addon64" ^
  --addon "%ROOT%release\renodx-dlss5Super-unified-neural-resolution-v6.1.addon64" || exit /b 1
endlocal
