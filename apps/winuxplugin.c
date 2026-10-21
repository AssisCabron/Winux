/* Winux plugin specimen: an ordinary x64 Windows DLL, no Microsoft binary code. */
#pragma comment(linker, "/export:WinuxPluginVersion,DATA")
#pragma comment(linker, "/export:WinuxPluginAttached,DATA")
#pragma comment(linker, "/export:WinuxPluginChecksum")
#pragma comment(linker, "/export:WinuxPluginTicks")
#define WINUXPLUGIN_BUILD 1
#include "winuxplugin_api.h"
typedef WinuxU32 U32;
__declspec(dllimport) U32 GetTickCount(void);

U32 WinuxPluginVersion = 0x10002;
U32 WinuxPluginAttached;

U32 WinuxPluginChecksum(const unsigned char *data, U32 length) {
    if (!WinuxPluginAttached || !data || length > 4096) return 0;
    U32 hash = 2166136261u;
    for (U32 i = 0; i < length; i++) {
        hash ^= (U32)data[i];
        hash *= 16777619u;
    }
    return hash;
}

/* Proves that a DLL export can call an existing Winux-provided Win32 service. */
U32 WinuxPluginTicks(void) {
    return WinuxPluginAttached ? GetTickCount() : 0;
}

int DllMain(void *module, U32 reason, void *reserved) {
    (void)module;
    (void)reserved;
    if (reason == 1) {
        if (WinuxPluginAttached || WinuxPluginVersion != 0x10002) return 0;
        WinuxPluginAttached = 1;
    } else if (reason == 0) {
        WinuxPluginAttached = 0;
    }
    return 1;
}
