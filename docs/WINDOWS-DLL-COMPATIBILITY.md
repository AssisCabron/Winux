# Windows DLL compatibility — Winux custom DLL fixture

## Real scope

The existing Winux PE32+ x64 loader maps a trusted EXE and its DLLs, binds
imports and exports and runs static DLL attach/detach. This fixture proves
that exactly the same self-authored DLL and EXE bytes run both under Windows
and inside the Winux P4 guest.

Windows DLLs require a host executable; they do not execute alone. No
permission from Microsoft is necessary to create a DLL for Windows.
Windows Defender/SmartScreen or enterprise security software may still scan
a binary on Windows. We cannot guarantee otherwise.

## Reused implementation

- apps/winuxplugin_api.h — single API contract shared by DLL and EXE.
- apps/winuxplugin.c — plugin with DllMain, writable exports, checksum and
  a call to the Win32 GetTickCount API.
- apps/winuxplugin.def — Windows import library export manifest.
- apps/plugin_fixture.c — CRT-free PE EXE importing plugin and kernel32 APIs.
- experiments/plugin/boot.c — P4 kernel, identical shared shell and loader.
  Only one compile-time preview flag adds the trusted test executable.
- scripts/build/build_plugin_preview.ps1 — compile both native Windows PE files.
- tests/runners/test_plugin_windows.py and
  scripts/build/test_plugin_windows.ps1 — Windows host comparison.
- scripts/build/make_plugin_preview.py — isolate plugin RAMFS and reuse
  the existing UEFI ISO packer; no new loader, ABI, heap or kernel.
- tests/runners/test_console_wrapper_fls_candidate.py --check-plugin —
  execute and verify plugin inside the Winux guest.

## Reproduce

From Winux repository root, run:

    .\scripts\build\verify_plugin_compat.ps1 -Python "C:\Program Files\Python310\python.exe"

Artifacts and test evidence are saved under:

    build/plugin-preview/plugin-windows-test.json
    build/plugin-preview/plugin-build.json
    build/plugin-preview/winux-0.38-wrapper-fls-candidate.iso
    build/console-plugin-p4-test.json
    build/console-plugin-p4-serial.log

The binary hashes in the host fixture, guest RAMFS and guest EFI are
compared. Stable ISO 0.38 and P4 historical images are not overwritten.

## Limitations

- The test executable runs at CPL0; unknown DLLs can crash/corrupt the kernel.
- The allowlist does not grant arbitrary DLL/EXE execution. Only the explicit
  trusted fixture is enabled by the WINUX_PLUGIN_PREVIEW build flag.
- Dynamic LoadLibrary/FreeLibrary for external disk files, general filesystem,
  process isolation, multithreading, SEH/unwind, Windows service management and
  CLR remain separate engineering milestones.
- The test uses no guest network (-nic none). This does not prove a Windows
  installation would never scan the DLL.
- No DuetOS source file was copied. Existing loader and kernel runtime code
  are reused, while REA research remains the source of Windows contracts.
