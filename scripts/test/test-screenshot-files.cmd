@echo off
setlocal
for %%I in ("%~dp0..\..") do set "ROOT=%%~fI\"
if "%~1"=="" exit /b 1
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /Fo"%ROOT%build\screenshot_files.obj" /Fe"%ROOT%build\screenshot_files.exe" "%ROOT%tests\screenshot_files.cpp" /link ole32.lib windowscodecs.lib uuid.lib || exit /b 1
"%ROOT%build\screenshot_files.exe" "%~1" || exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /Fo"%ROOT%build\nr_activity_monitor.obj" /Fe"%ROOT%build\nr_activity_monitor.exe" "%ROOT%tests\nr_activity_monitor.cpp" || exit /b 1
"%ROOT%build\nr_activity_monitor.exe" || exit /b 1
endlocal
