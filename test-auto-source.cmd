@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /O2 /Fo"%~dp0build\auto_source_policy.obj" /Fe"%~dp0build\auto_source_policy.exe" "%~dp0tests\auto_source_policy.cpp" || exit /b 1
"%~dp0build\auto_source_policy.exe" || exit /b 1
