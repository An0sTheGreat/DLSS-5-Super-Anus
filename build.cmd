@echo off
setlocal

call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if not "%errorlevel%"=="0" exit /b %errorlevel%

if not exist build mkdir build

"C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\dxc.exe" /nologo /T cs_6_0 /E Resample /Fo build\neural_resample.cso src\neural_resample.hlsl
if not "%errorlevel%"=="0" exit /b %errorlevel%

python tools\binary_to_header.py build\neural_resample.cso src\neural_resample_shader.hpp g_neural_resample_shader
if not "%errorlevel%"=="0" exit /b %errorlevel%

cl.exe /nologo /std:c++20 /EHsc /MT /O2 /GL /W4 /LD ^
  /I "C:\tmp\reshade-source\include" ^
  /I "C:\tmp\imgui-source" ^
  src\neural_resolution_addon.cpp ^
  /link /LTCG /OPT:REF /OPT:ICF /OUT:build\renodx-neural-resolution.addon64 Psapi.lib User32.lib
exit /b %errorlevel%
