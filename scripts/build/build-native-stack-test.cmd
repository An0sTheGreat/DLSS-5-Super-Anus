@echo off
setlocal
for %%I in ("%~dp0..\..") do set "ROOT=%%~fI\"
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cl /nologo /W4 /O2 /MT /Fe:"%ROOT%build\native_host_stack.exe" /Fo:"%ROOT%build\native_host_stack.obj" "%ROOT%tests\native_host_stack.cpp" /link dbghelp.lib psapi.lib || exit /b 1
endlocal
