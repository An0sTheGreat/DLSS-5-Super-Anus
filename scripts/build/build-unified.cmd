@echo off
setlocal
for %%I in ("%~dp0..\..") do set "ROOT=%%~fI\"
set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
call "%VCVARS%" >nul || exit /b 1
if not exist "%ROOT%build" mkdir "%ROOT%build"
cl /nologo /c /O2 /Oi- /GS- /GR- /EHs-c- /guard:cf- /Zl /Brepro /Fo"%ROOT%build\unified_controls.obj" "%ROOT%src\unified_controls.cpp" || exit /b 1
ml64 /nologo /c /Fo"%ROOT%build\unified_hooks.obj" "%ROOT%src\unified_hooks.asm" || exit /b 1
link /nologo /dll /nodefaultlib /entry:unified_entry /fixed /dynamicbase:no /incremental:no /Brepro /opt:noref /merge:.rdata=.text /merge:.data=.text /section:.text,erw /map:"%ROOT%build\unified_blob.map" /out:"%ROOT%build\unified_blob.dll" "%ROOT%build\unified_controls.obj" "%ROOT%build\unified_hooks.obj" || exit /b 1
python "%ROOT%tools\patch_unified_addon.py" --base "%ROOT%updated-official-renodx-dlss.addon64" --blob "%ROOT%build\unified_blob.dll" --map "%ROOT%build\unified_blob.map" --output "%ROOT%release\renodx-dlss5Super-unified-numpad-controls-v5.addon64" || exit /b 1
endlocal
