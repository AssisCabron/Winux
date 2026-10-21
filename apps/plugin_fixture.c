/* Host EXE for custom Windows DLL. Runs unchanged on Windows and inside Winux. */
#include "winuxplugin_api.h"
typedef WinuxU32 U32;
typedef unsigned short U16;
#define IMPORT __declspec(dllimport)
IMPORT void *GetModuleHandleW(const U16 *name);
IMPORT void *GetProcAddress(void *module, const char *name);
IMPORT U32 GetTickCount(void);
IMPORT void *GetStdHandle(U32 kind);
IMPORT int WriteFile(void *handle, const void *data, U32 length, U32 *written, void *overlapped);

int mainCRTStartup(void) {
    if (WinuxPluginAttached != 1 || WinuxPluginVersion != 0x10002) return 10;
    static const unsigned char payload[] = "Winux custom DLL running without a Microsoft DLL";
    U32 expected = 2166136261u;
    for (U32 i = 0; i < sizeof(payload)-1; i++) {
        expected ^= (U32)payload[i];
        expected *= 16777619u;
    }
    if (WinuxPluginChecksum(payload, (U32)sizeof(payload)-1) != expected) return 11;
    if (WinuxPluginChecksum(payload, 4097) != 0) return 12;
    void *module = GetModuleHandleW(L"winuxplugin.dll");
    if (!module) return 13;
    U32 (*checksum)(const unsigned char *, U32) =
        (U32(*)(const unsigned char *, U32))GetProcAddress(module, "WinuxPluginChecksum");
    U32 (*ticks)(void) = (U32(*)(void))GetProcAddress(module, "WinuxPluginTicks");
    if (!checksum || !ticks || checksum(payload, (U32)sizeof(payload)-1) != expected) return 14;
    if (GetProcAddress(module, "NoSuchPluginExport")) return 15;
    U32 before = GetTickCount(), after = ticks();
    if (after < before) return 16;
    static const char message[] = "WINUX_CUSTOM_DLL_OK: DllMain, exports, data, GetProcAddress, GetTickCount\r\n";
    U32 written = 0;
    if (!WriteFile(GetStdHandle((U32)-11), message, (U32)sizeof(message)-1, &written, 0) ||
        written != (U32)sizeof(message)-1) return 17;
    return 0;
}
