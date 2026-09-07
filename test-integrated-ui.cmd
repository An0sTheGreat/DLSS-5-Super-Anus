@echo off
setlocal
set "ROOT=%~dp0"
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /DNR_DX11_GAME_TEST /I "%ROOT%src" /I "C:\tmp\imgui-source" /I "C:\tmp\reshade-source\include" ^
 /Fo"%ROOT%build\\" /Fe"%ROOT%build\regression_integrated_ui.exe" ^
 "%ROOT%tests\regression_v66.cpp" "C:\tmp\imgui-source\imgui.cpp" ^
 "C:\tmp\imgui-source\imgui_draw.cpp" "C:\tmp\imgui-source\imgui_widgets.cpp" ^
 "C:\tmp\imgui-source\imgui_tables.cpp" || exit /b 1
"%ROOT%build\regression_integrated_ui.exe" || exit /b 1
"%ROOT%build\regression_v63.exe" || exit /b 1
"%ROOT%build\regression_v64.exe" || exit /b 1
"%ROOT%build\regression_v65.exe" || exit /b 1
endlocal
