@echo off
setlocal
for %%I in ("%~dp0..\..") do set "ROOT=%%~fI\"

call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if not "%errorlevel%"=="0" exit /b %errorlevel%

if not exist "%ROOT%build" mkdir "%ROOT%build"

"C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\dxc.exe" /nologo /T cs_6_0 /E Resample /Fo "%ROOT%build\neural_resample.cso" "%ROOT%src\neural_resample.hlsl"
if not "%errorlevel%"=="0" exit /b %errorlevel%

python "%ROOT%tools\binary_to_header.py" "%ROOT%build\neural_resample.cso" "%ROOT%src\neural_resample_shader.hpp" g_neural_resample_shader
if not "%errorlevel%"=="0" exit /b %errorlevel%

cl.exe /nologo /std:c++20 /EHsc /MT /O2 /GL /W4 /LD ^
  /I "C:\tmp\reshade-source\include" ^
  /I "C:\tmp\imgui-source" ^
  "%ROOT%src\neural_resolution_addon.cpp" ^
  /link /LTCG /OPT:REF /OPT:ICF /OUT:"%ROOT%build\renodx-neural-resolution.addon64" Psapi.lib User32.lib
exit /b %errorlevel%
