@echo off
setlocal
set "ROOT=%~dp0"
if not exist "%ROOT%build\vulkan-headers-api\include\vulkan\vulkan.h" (
 echo Missing pinned Khronos Vulkan-Headers dependency. See implementation plan.
 exit /b 1
)
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cl /nologo /W4 /WX /EHsc /O2 /MT /std:c++17 /I"%ROOT%build\dlss-sdk-api\include" /I"%ROOT%build\vulkan-headers-api\include" ^
 /Fe:"%ROOT%build\vulkan-ngx-probe.exe" /Fo:"%ROOT%build\vulkan-ngx-probe.obj" "%ROOT%tests\vulkan_ngx_probe.cpp" ^
 /link user32.lib advapi32.lib shlwapi.lib bcrypt.lib "%ROOT%build\dlss-sdk-api\lib\Windows_x86_64\x64\nvsdk_ngx_s.lib" || exit /b 1
cl /nologo /W4 /WX /EHsc /O2 /MT /std:c++17 /I"%ROOT%build\dlss-sdk-api\include" /I"%ROOT%build\vulkan-headers-api\include" ^
 /Fe:"%ROOT%build\vulkan-binding-guard.exe" /Fo:"%ROOT%build\vulkan-binding-guard.obj" "%ROOT%tests\vulkan_binding_guard.cpp" /link bcrypt.lib || exit /b 1
endlocal
