#pragma once
#include <Windows.h>
namespace nr::screenshots {
// Read-only Windows SDR white level for this game's monitor. No global display
// settings or game exposure settings are changed. A failed query is explicit.
inline float display_white_nits(HWND window)
{
    MONITORINFOEXW monitor = {}; monitor.cbSize = sizeof(monitor);
    if (!window || !GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONULL),&monitor)) return 0;
    UINT32 paths = 0, modes = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS,&paths,&modes) != ERROR_SUCCESS ||
        !paths || paths > 128 || modes > 512) return 0;
    auto *path = static_cast<DISPLAYCONFIG_PATH_INFO *>(HeapAlloc(GetProcessHeap(),0,paths*sizeof(DISPLAYCONFIG_PATH_INFO)));
    auto *mode = static_cast<DISPLAYCONFIG_MODE_INFO *>(HeapAlloc(GetProcessHeap(),0,(modes ? modes : 1)*sizeof(DISPLAYCONFIG_MODE_INFO)));
    float result = 0;
    if (path && mode && QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS,&paths,path,&modes,mode,nullptr) == ERROR_SUCCESS) {
        for (UINT32 i = 0; i < paths; ++i) {
            DISPLAYCONFIG_SOURCE_DEVICE_NAME source = {};
            source.header = {DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME,sizeof(source),path[i].sourceInfo.adapterId,path[i].sourceInfo.id};
            if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS ||
                lstrcmpiW(source.viewGdiDeviceName,monitor.szDevice) != 0) continue;
            DISPLAYCONFIG_SDR_WHITE_LEVEL white = {};
            white.header = {DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL,sizeof(white),path[i].targetInfo.adapterId,path[i].targetInfo.id};
            if (DisplayConfigGetDeviceInfo(&white.header) == ERROR_SUCCESS && white.SDRWhiteLevel >= 1000 && white.SDRWhiteLevel <= 125000)
                result = white.SDRWhiteLevel * .08f;
            break;
        }
    }
    if (path) HeapFree(GetProcessHeap(),0,path);
    if (mode) HeapFree(GetProcessHeap(),0,mode);
    return result;
}
}
