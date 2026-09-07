@echo off
setlocal
for %%I in ("%~dp0..\..") do set "ROOT=%%~fI\"
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /O2 /Fo"%ROOT%build\auto_source_policy.obj" /Fe"%ROOT%build\auto_source_policy.exe" "%ROOT%tests\auto_source_policy.cpp" || exit /b 1
"%ROOT%build\auto_source_policy.exe" || exit /b 1
