@echo off
setlocal
for %%I in ("%~dp0..\..") do set "ROOT=%%~fI\"
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /I "C:\tmp\imgui-source" /Fo"%ROOT%build\\" /Fe"%ROOT%build\input_controls_ui.exe" ^
 "%ROOT%tests\input_controls_ui.cpp" "C:\tmp\imgui-source\imgui.cpp" "C:\tmp\imgui-source\imgui_draw.cpp" ^
 "C:\tmp\imgui-source\imgui_widgets.cpp" "C:\tmp\imgui-source\imgui_tables.cpp" /link user32.lib || exit /b 1
"%ROOT%build\input_controls_ui.exe" || exit /b 1
endlocal
