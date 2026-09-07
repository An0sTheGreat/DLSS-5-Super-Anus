@echo off
setlocal
for %%I in ("%~dp0..\..") do set "ROOT=%%~fI\"
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
for %%T in (capture_chain key_binding_policy nr_activity_monitor final_capture_policy) do (
 cl /nologo /std:c++20 /EHsc /W4 /WX /Fo"%ROOT%build\%%T.obj" /Fe"%ROOT%build\%%T.exe" "%ROOT%tests\%%T.cpp" || exit /b 1
 "%ROOT%build\%%T.exe" || exit /b 1
)
endlocal
