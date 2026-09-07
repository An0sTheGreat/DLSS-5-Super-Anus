@echo off
setlocal
for %%I in ("%~dp0..\..") do set "ROOT=%%~fI\"
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
if not exist "%ROOT%build" mkdir "%ROOT%build"
if not exist "%ROOT%release" mkdir "%ROOT%release"

cl /nologo /std:c++20 /EHsc /W4 /I "%ROOT%src" /I "C:\tmp\imgui-source" ^
  /Fo"%ROOT%build\\" /Fe"%ROOT%build\regression_v63.exe" ^
  "%ROOT%tests\regression_v63.cpp" "C:\tmp\imgui-source\imgui.cpp" ^
  "C:\tmp\imgui-source\imgui_draw.cpp" "C:\tmp\imgui-source\imgui_widgets.cpp" ^
  "C:\tmp\imgui-source\imgui_tables.cpp" || exit /b 1
"%ROOT%build\regression_v63.exe" || exit /b 1

"C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\dxc.exe" /nologo /T cs_6_0 /E Resample /Fo "%ROOT%build\neural_resample_v63.cso" "%ROOT%src\neural_resample.hlsl" || exit /b 1
python "%ROOT%tools\binary_to_header.py" "%ROOT%build\neural_resample_v63.cso" "%ROOT%src\neural_resample_shader.hpp" g_neural_resample_shader || exit /b 1
cl /nologo /c /std:c++20 /EHs-c- /O2 /Oi /GS- /GR- /guard:cf- /Zl /Brepro /W4 /WX ^
  /I "C:\tmp\reshade-source\include" /I "C:\tmp\imgui-source" ^
  /Fo"%ROOT%build\neural_resolution_v63.obj" "%ROOT%src\neural_resolution_addon.cpp" || exit /b 1
ml64 /nologo /c /Fo"%ROOT%build\embedded_bridges_v63.obj" "%ROOT%src\embedded_bridges.asm" || exit /b 1
link /nologo /dll /nodefaultlib /entry:combined_entry /dynamicbase /incremental:no /Brepro /opt:ref /opt:icf ^
  /map:"%ROOT%build\neural_resolution_v63.map" /out:"%ROOT%build\neural_resolution_v63_embedded.dll" ^
  "%ROOT%build\neural_resolution_v63.obj" "%ROOT%build\embedded_bridges_v63.obj" ^
  kernel32.lib Psapi.lib User32.lib || exit /b 1
python "%ROOT%tools\patch_v6_addon.py" --base "%ROOT%updated-official-renodx-dlss.addon64" ^
  --embedded "%ROOT%build\neural_resolution_v63_embedded.dll" --map "%ROOT%build\neural_resolution_v63.map" ^
  --output "%ROOT%release\renodx-dlss5-super-anus.addon64" || exit /b 1
python "%ROOT%tools\validate_v6_addon.py" --base "%ROOT%updated-official-renodx-dlss.addon64" ^
  --addon "%ROOT%release\renodx-dlss5-super-anus.addon64" || exit /b 1
endlocal
