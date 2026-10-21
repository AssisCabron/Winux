# Winux 0.38-P4: KernelBase heap/NTDLL realloc/FLS milestone

Run date: 2026-10-21. Experimental research only. Stable 0.38 image and prior candidates not altered.

## Images and identities

| Image | SHA-256 | Validation |
|---|---|---|
| `build/winux-0.38-x64.iso` | `700e6a38c0b7dbe312b6012810728af8ecd24a984cd8287e0f644a73f1c343b0` | Previous baseline preserved |
| `build/winux-0.38-wrapper-realloc-candidate.iso` | `4ab7b463d4b8f79b016e648bf70c37d09be05640d67375bc497513ac5563ac2a` | P3: QEMU 37/37 |
| `build/winux-0.38-wrapper-fls-candidate.iso` | `4d1e20074e35fb9b03bbcb3043180cac39c08cd8c5536efc784e48a7e9270c77` | P4: QEMU 37/37 |

The 37 count refers to *regression checklist groups*, not a claim of 37 Windows APIs or 37 independent suites. `build/console-wrapper-fls-candidate-test.json`: `passed=true`, `failure=null`, elapsed 73.14 seconds (test runtime, **not boot-time performance**). QEMU q35 UEFI, TCG, 256 MiB, 1 virtual CPU, network disabled.

## New confirmed contracts

### P3 (carried into P4)
- Original `KernelBase!HeapSetInformation` calls reach original `NTDLL!RtlSetHeapInformation`, with TEB `LastError` conversion.
- Invalid input size: returns `FALSE`, Win32 last error 122; invalid compatibility value: `FALSE`, error 31.
- Heap compatibility policy value 2 can be set and repeated on a private heap; earlier NTDLL validation probes reject `STATUS_BUFFER_TOO_SMALL` and `STATUS_UNSUCCESSFUL`.
- Original NTDLL allocate/free works on requested 8,16,32,64,128,512,4096,65536 byte zeroed buffers after policy 2.
- Original `RtlReAllocateHeap` preserves bytes on 32->128, 128->2048, 4096->8192, 128->16; extension zeroed on growth, `RtlSizeHeap` reports requested length, `RtlFreeHeap` succeeds.
- Windows comparison: `probe_heap_wrapper_realloc_windows.py` -> `build/heap-wrapper-realloc-windows.json`. P3 tests twice in QEMU.

### P4
- Original `KernelBase!FlsAlloc/FlsSetValue/FlsGetValue/FlsFree` invoked with guest TEB and persistent NTDLL heap context. Calls exercise actual original KernelBase/NTDLL code, not test shims.
- Explicit `FlsSetValue(index, NULL)` sets up guest FLS state before empty read; the successful `FlsGetValue` clears guest `LastError`.
- Two value overwrites preserve callback count 0 until `FlsFree`; the free triggers callback with the final value `0x2222`.
- Invalid Get/Set/Free index `0xffffffff` report Win32 error 87.
- A second FLS index retains value `0x3333` until `nt_native_dispose`; original `RtlProcessFlsData` invokes callback before heap teardown, and guest TEB FLS pointer is cleared.
- Both diagnostic cycles printed `KB_FLS_WRAPPERS_OK` and `KB_FLS_TEARDOWN_OK`; normal startup, heap and process cleanup checks still pass.
- Windows host comparison: `probe_kernelbase_fls_windows.py` -> `build/kernelbase-fls-windows.json`. Host callback on FlsFree observed; host process cleanup is not deliberately altered for the guest-only teardown test.

## Files to reproduce, all relative to experimental-os

- Generator: `prepare_kernelbase_fls_candidate.py`, cloned from `prepare_heap_wrapper_realloc_candidate.py`.
- Candidate C source: `src/nt_startup_wrapper_fls_candidate.h`, `src/shell_wrapper_fls_candidate.h`, `src/boot_wrapper_fls_candidate.c`.
- Candidate isolated ISO packer: `make_iso_wrapper_fls_candidate.py`.
- Candidate regression: `test_console_wrapper_fls_candidate.py`.
- Results: `build/console-wrapper-fls-candidate-test.json`, `build/console-wrapper-fls-candidate-serial.log`, `build/console-wrapper-fls-candidate.ppm`.
- Windows reference: `build/heap-wrapper-realloc-windows.json`, `build/kernelbase-fls-windows.json`.
- REA dossiers for FLS, KernelBase and NTDLL already retained under `../windows-analysis/cmd/`.

Repeat full candidate regression with `C:\Program Files\Python310\python.exe test_console_wrapper_fls_candidate.py` from project root. The standard 0.38 runner was not changed. The candidate test consumes the raw QEMU PPM screenshot instead of requiring optional Pillow.

## Restrictions and next engineering gate

- No `MSVCRT.dll` DllMain / CRT initializer was called; no CMD execution or general native Win32 app support.
- Guest executable fixtures still run at CPL0 with no isolation. Never relax the allowlist for third-party EXEs.
- No complete LFH across concurrency/fragmentation/failure paths was demonstrated.
- No VirtualBox, Proxmox, hardware, multicore, persistent disk, CLR or driver verification for this candidate.
- Microsoft-owned DLLs remain under licensed Windows usage concerns; do not distribute without appropriate rights.
- Next target is *a controlled, version-pinned CRT initializer attach/detach harness* once dependencies in heap policy, FLS, CRT locale, process parameters, console descriptors, static C/C++ constructors and exception/unwind paths are enumerated and guarded.
- Before permitting execution of unknown code, implement CPL3, per-process page tables and validated syscall boundary; static DLL import resolution alone is insufficient.
