# Winux 0.38-P1 experimental: original NTDLL heap policy

Validation date: 2026-10-21. The 0.38 baseline was not rebuilt or overwritten.

## Images and hashes

| Image | SHA-256 | Status |
| --- | --- | --- |
| build/winux-0.38-x64.iso | 700e6a38c0b7dbe312b6012810728af8ecd24a984cd8287e0f644a73f1c343b0 | Original baseline unchanged |
| build/winux-0.38-heap-policy-candidate.iso | 8b9fdd4869c8ed1291ee4b1d58ddc57a5f4fc84444209972dd4e988334f0f086 | QEMU functional regression passed |

Ghidra 12.1.4 / REA 6.4.0 on a matching Windows 10 build, same NTDLL SHA-256 as previous project dossiers.

## What changed in the candidate

- Added an isolated kernel test using original `ntdll.dll!RtlSetHeapInformation` for HeapCompatibilityInformation (class 0) on a guest private heap originally created by KernelBase and initialized through the native context.
- Calls reject a zero input length with NTSTATUS `0xc0000023`, reject value 1 with `0xc0000001`, accept value 2 with `0`, accept a repeated request and verify the expected version-specific internal byte value. Private heap is then destroyed normally.
- This change runs only when the `nt-startup-test` shell diagnostic is requested. It **does not** initialize MSVCRT, execute `cmd.exe` or remove the PE loader allowlist.
- The stable original kernel source and ISO are not replaced; all modified includes were cloned under candidate-specific filenames.

## Evidence and files

- Analysis: `../windows-analysis/cmd/heap-policy-2026-10-21.md`.
- Main original API dossier: `../windows-analysis/cmd/ntdll-RtlSetHeapInformation-rea-20261021.json`, evidence `ev_bdf4fdcd64c929dc96ebdb240a24649bfdf237a43391a0e524884a4c1d821f7d`.
- Helper dossier: `../windows-analysis/cmd/ntdll-heap-policy-helper-rea-20261021.json`, evidence `ev_b022894ea52ad2e635e418adbe42746e7af3960770ca265c937389f0c48519ce`.
- Local Windows comparison script: `probe_heap_policy.py`, results `build/heap-policy-windows-probe.json`.
- Candidate generator `prepare_heap_policy_candidate.py`.
- Candidate sources: `src/nt_startup_heap_policy_candidate.h`, `src/shell_heap_policy_candidate.h`, `src/boot_heap_policy_candidate.c`.
- Candidate ISO packer `make_iso_heap_policy_candidate.py`.
- Candidate regression generator `prepare_heap_policy_candidate_test.py`, runner `test_console_heap_policy_candidate.py`.
- Reproducible regression result `build/console-heap-candidate-test.json`, full serial log `build/console-heap-candidate-serial.log`, framebuffer `build/console-heap-candidate.ppm`.

## Verification performed

- QEMU system x86_64 with UEFI q35, TCG, 256 MiB, 1 vCPU and no network.
- Full candidate regression: `passed:true`, 37 recorded checks, 71.88 seconds total scripted execution. **These seconds are the test harness runtime and NOT an OS boot-time benchmark.**
- `NT_HEAP_POLICY_OK` observed **twice** during `nt-startup-test`, with expected status values; native context cleanup and the standard private-heap checks remained green.
- The console regression also confirmed the original six Windows-format executable fixtures, two DLLs, eight mapped Windows modules and 3,405 regular imports resolved **without DLL initialization or CMD execution**.
- The QEMU screenshot check was changed in the copied candidate test to validate the generated PPM directly because Pillow was not installed on that Python runtime; this is a test dependency change only.

## What this does NOT prove

- Does not establish allocation/reallocation/free behavior after LFH policy activation across block sizes or exhaustion, nor a complete LFH implementation.
- No original MSVCRT `DllMain` attach/detach or CMD execution has been validated.
- No guest user mode (CPL3), process memory isolation, scheduler, persistent filesystem, .NET CLR or drivers.
- VirtualBox, Proxmox and physical hardware were **not** tested against this candidate.
- Binary-internal offsets are pinned to this exact NTDLL SHA, not applicable as universal Windows layouts.

## Next engineering gate

1. Add a separate comparator for post-policy heap allocations (tiny/medium/large), alignment, zeroing, realloc, free and heap destruction on both real Windows and guest; preserve the now-passing ISO candidate.
2. Close CRT's `HeapSetInformation` wrapper, `GetStartupInfoW`, CRT standard I/O descriptor ownership, ANSI environment and FLS/locale initializer under one *persistent* native context. Compare outputs and cleanup against the real Windows build.
3. Only after a safe, complete guest CRT attach/detach should the original CMD be considered for a controlled `cmd.exe /d /c echo teste` test. Arbitrary EXE launch must remain forbidden in CPL0; engineer separate CPL3, page tables and process boundaries first.
4. Repeat QEMU/VirtualBox, negative paths and resource baselines before tagging any stable successor to 0.38. Keep original hash and ISO unchanged.

## Repeat verification

Run `C:\Program Files\Python310\python.exe test_console_heap_policy_candidate.py` from the `experimental-os` directory.
