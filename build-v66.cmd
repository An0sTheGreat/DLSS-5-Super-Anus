@echo off
setlocal
set "ROOT=%~dp0"
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
"C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\fxc.exe" /nologo /T cs_5_0 /E main /O3 /Fo "%ROOT%build\screenshot_copy.cso" "%ROOT%src\screenshot_copy.hlsl" || exit /b 1
python "%ROOT%tools\binary_to_header.py" "%ROOT%build\screenshot_copy.cso" "%ROOT%src\screenshot_copy_shader.hpp" g_screenshot_copy_shader || exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /I "%ROOT%src" /I "C:\tmp\reshade-source\include" /Fo"%ROOT%build\regression_v65.obj" /Fe"%ROOT%build\regression_v65.exe" "%ROOT%tests\regression_v65.cpp" || exit /b 1
"%ROOT%build\regression_v65.exe" || exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /I "%ROOT%src" /Fo"%ROOT%build\regression_v64.obj" /Fe"%ROOT%build\regression_v64.exe" "%ROOT%tests\regression_v64.cpp" || exit /b 1
"%ROOT%build\regression_v64.exe" || exit /b 1
cl /nologo /std:c++20 /EHsc /O2 /W4 /WX /I "C:\tmp\reshade-source\include" /I "C:\tmp\imgui-source" ^
  /Fo"%ROOT%build\lifetime_integration_v64.obj" /Fe"%ROOT%build\lifetime_integration_v64.exe" ^
  "%ROOT%tests\lifetime_integration_v64.cpp" /link d3d12.lib dxgi.lib user32.lib ole32.lib windowscodecs.lib uuid.lib || exit /b 1
"%ROOT%build\lifetime_integration_v64.exe" || exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /I "%ROOT%src" /I "C:\tmp\imgui-source" ^
  /Fo"%ROOT%build\\" /Fe"%ROOT%build\regression_v63.exe" ^
  "%ROOT%tests\regression_v63.cpp" "C:\tmp\imgui-source\imgui.cpp" ^
  "C:\tmp\imgui-source\imgui_draw.cpp" "C:\tmp\imgui-source\imgui_widgets.cpp" ^
  "C:\tmp\imgui-source\imgui_tables.cpp" || exit /b 1
"%ROOT%build\regression_v63.exe" || exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /I "%ROOT%src" /I "C:\tmp\imgui-source" /I "C:\tmp\reshade-source\include" ^
  /Fo"%ROOT%build\\" /Fe"%ROOT%build\regression_v66.exe" ^
  "%ROOT%tests\regression_v66.cpp" "C:\tmp\imgui-source\imgui.cpp" ^
  "C:\tmp\imgui-source\imgui_draw.cpp" "C:\tmp\imgui-source\imgui_widgets.cpp" ^
  "C:\tmp\imgui-source\imgui_tables.cpp" || exit /b 1
"%ROOT%build\regression_v66.exe" || exit /b 1
"C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\dxc.exe" /nologo /T cs_6_0 /E Resample /Fo "%ROOT%build\neural_resample_v66.cso" "%ROOT%src\neural_resample.hlsl" || exit /b 1
python "%ROOT%tools\binary_to_header.py" "%ROOT%build\neural_resample_v66.cso" "%ROOT%src\neural_resample_shader.hpp" g_neural_resample_shader || exit /b 1
cl /nologo /c /std:c++20 /MD /EHs-c- /O2 /Oi /GS- /GR- /guard:cf- /Zl /Brepro /W4 /WX ^
  /I "C:\tmp\reshade-source\include" /I "C:\tmp\imgui-source" ^
  /Fo"%ROOT%build\neural_resolution_v66.obj" "%ROOT%src\neural_resolution_addon.cpp" || exit /b 1
ml64 /nologo /c /Fo"%ROOT%build\embedded_bridges_v66.obj" "%ROOT%src\embedded_bridges.asm" || exit /b 1
link /nologo /dll /nodefaultlib /entry:combined_entry /dynamicbase /incremental:no /Brepro /opt:ref /opt:icf ^
  /map:"%ROOT%build\neural_resolution_v66.map" /out:"%ROOT%build\neural_resolution_v66_embedded.dll" ^
  "%ROOT%build\neural_resolution_v66.obj" "%ROOT%build\embedded_bridges_v66.obj" ^
  kernel32.lib Psapi.lib User32.lib ucrt.lib ole32.lib windowscodecs.lib uuid.lib || exit /b 1
python "%ROOT%tools\patch_v6_addon.py" --base "%ROOT%updated-official-renodx-dlss.addon64" ^
  --embedded "%ROOT%build\neural_resolution_v66_embedded.dll" --map "%ROOT%build\neural_resolution_v66.map" ^
  --section-name .nr-v66 --screenshot-capture --output "%ROOT%build\v66-candidate.addon64" || exit /b 1
python "%ROOT%tools\validate_v6_addon.py" --base "%ROOT%updated-official-renodx-dlss.addon64" ^
  --version V6.6 --screenshot-capture --addon "%ROOT%build\v66-candidate.addon64" || exit /b 1
endlocal
