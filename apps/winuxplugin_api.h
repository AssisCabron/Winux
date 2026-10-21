/* Shared contract for the self-authored Windows DLL and its EXE fixture. */
#ifndef WINUXPLUGIN_API_H
#define WINUXPLUGIN_API_H
typedef unsigned int WinuxU32;
#ifdef WINUXPLUGIN_BUILD
#define WINUXPLUGIN_API
#else
#define WINUXPLUGIN_API __declspec(dllimport)
#endif
extern WINUXPLUGIN_API WinuxU32 WinuxPluginVersion;
extern WINUXPLUGIN_API WinuxU32 WinuxPluginAttached;
WINUXPLUGIN_API WinuxU32 WinuxPluginChecksum(const unsigned char *data, WinuxU32 length);
WINUXPLUGIN_API WinuxU32 WinuxPluginTicks(void);
#endif
