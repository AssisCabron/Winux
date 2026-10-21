# Winux 0.38-P2: original NTDLL allocations after policy 2

Date: 2026-10-21. Experimental candidate image; original stable 0.38 and P1 candidate preserved.

## Image identity and outcomes

| Artifact | SHA-256 | Test |
| --- | --- | --- |
| build/winux-0.38-x64.iso | 700e6a38c0b7dbe312b6012810728af8ecd24a984cd8287e0f644a73f1c343b0 | Original baseline unchanged |
| build/winux-0.38-heap-policy-candidate.iso | 8b9fdd4869c8ed1291ee4b1d58ddc57a5f4fc84444209972dd4e988334f0f086 | P1 37 QEMU regression groups passed |
| build/winux-0.38-heap-policy-alloc-candidate.iso | 49e96293a67cbb521bd07fa2d4c58b78f0c7eac8c6b7d55f46bbd72dadc11671 | **P2 37 QEMU regression groups passed** |

P2 QEMU runtime 73.83 s reflects scripted test duration, not boot time; emulated q35/UEFI, 256 MiB, one virtual CPU, no network, TCG.

## Verified Windows and Winux behavior

Windows host script `probe_heap_policy_allocations.py` on private heap:
- HeapCreate(0,4096,0), original NTDLL RtlSetHeapInformation(class0, DWORD 2, size4) -> STATUS_SUCCESS.
- Sizes 8, 16, 32, 64, 128, 512, 4096, 65536 bytes via original RtlAllocateHeap(heap, HEAP_ZERO_MEMORY, size).
- Each allocation succeeded, memory initially zero, last byte writable with value 0x5a, and RtlFreeHeap succeeded; HeapDestroy succeeded.
- Host evidence file: `build/heap-policy-windows-allocations.json`.

P2 guest uses same input sizes and original NTDLL exports on a native-context guest private heap. Two runs of `nt-startup-test` emitted:
- `NT_HEAP_POLICY_OK: NTDLL policy 2, invalid paths, repeat and ownership`
- `NT_LFH_ALLOCATION_OK: 8 sizes, zero, write, free after policy2`
- `KB_PRIVATE_HEAP_OK: original create/destroy, 64 KiB zero allocation, persistent system info`
- `NT_STARTUP_TEST_OK`

The original 37 regression groups remained green, including basic EFI boot, memory VM, PE fixtures, DLL import/attach, NTDLL memory syscalls, NT console, objects, cleanup and unchanged CMD block. This is empirical comparison of specific call paths, not a complete Low Fragmentation Heap implementation.

## Sources, reproducibility, files

- P1 REA evidence and dossier: `../windows-analysis/cmd/heap-policy-2026-10-21.md`.
- P2 candidate creator: `prepare_heap_policy_alloc_candidate.py`.
- P2 C sources: `src/nt_startup_heap_policy_alloc_candidate.h`, `src/shell_heap_policy_alloc_candidate.h`, `src/boot_heap_policy_alloc_candidate.c`.
- Separate packaging: `make_iso_heap_policy_alloc_candidate.py`.
- Executable EFI: `build/BOOTX64-heap-policy-alloc.EFI`.
- Candidate QEMU test: `test_console_heap_policy_alloc_candidate.py`.
- Detailed result: `build/console-heap-alloc-candidate-test.json`.
- Serial trace: `build/console-heap-alloc-candidate-serial.log`.
- Framebuffer capture: `build/console-heap-alloc-candidate.ppm`.
- Repeat from experimental-os: `C:\Program Files\Python310\python.exe test_console_heap_policy_alloc_candidate.py`.

## Limits and next release gate

- This experimental kernel runs the trusted diagnostic in CPL0. It has no separate user mode, isolation or general process scheduling.
- Changes live in candidate files only and do not relax the existing executable allowlist or execute the original CMD.
- Despite the correct policy state and allocations, this is **not** evidence of complete LFH implementation, CRT DllMain attach, Windows application support, or performance gains.
- Add original KernelBase HeapSetInformation path to guest tests and query-policy comparison, plus realloc, overflow, exhaustion, fragment/reuse and teardown cases.
- Then join actual MSVCRT attach, private heap, FLS, environment and CRT descriptors under a retained native loader context; compare success AND cleanup before attempting the original CMD path.
- Keep candidate out of production and untrusted inputs; do not distribute Microsoft's proprietary DLLs without satisfying applicable licensing rights.
- QEMU regression passed; VirtualBox/Proxmox and physical hardware were **not run** for this P2 candidate.
