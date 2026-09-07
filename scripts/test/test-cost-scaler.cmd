@echo off
setlocal
for %%I in ("%~dp0..\..") do set "ROOT=%%~fI\"
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
"C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\dxc.exe" /nologo /T cs_6_0 /E Resample /Fo "%ROOT%build\neural_resample_v66.cso" "%ROOT%src\neural_resample.hlsl" || exit /b 1
python "%ROOT%tools\binary_to_header.py" "%ROOT%build\neural_resample_v66.cso" "%ROOT%src\neural_resample_shader.hpp" g_neural_resample_shader || exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /Fe"%ROOT%build\cost_scaler_gpu.exe" /Fo"%ROOT%build\cost_scaler_gpu.obj" "%ROOT%tests\cost_scaler_gpu.cpp" /link d3d12.lib dxgi.lib || exit /b 1
"%ROOT%build\cost_scaler_gpu.exe" %* || exit /b 1
