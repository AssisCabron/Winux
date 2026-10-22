# Winux — Intel VT-d: isolated DMA experiment

Date: 2026-10-22. Status: EXPERIMENTAL, QEMU q35/TCG, 1 vCPU.
The stable Winux 0.38 ISO is unchanged. Never enable untrusted PCI passthrough or arbitrary Windows .SYS.

## Architectural boundary
- The UEFI boot context carries an actual ACPI 2.0 RSDP.
- `src/device/intel_vtd.h` checks the RSDP, XSDT and DMAR checksums, parses one segment-zero DRHD and its explicit PCI device scopes, rejects unsupported RMRR/multiple DRHD, then reads CAP/ECAP and legacy registers at the DRHD MMIO base.
- QEMU exposes DRHD at `0xFED90000`, with scoped VirtIO function `00:02.0`. **DRHD include-all is not required**; ownership must match a scoped BDF.
- Root/context entries address a 4-level second-level page table. The PCI function has a unique domain ID and separate root. Other PCI functions lack a present context entry and are denied; this prototype has one active owner because its 32-KiB coherent bounce pool is exclusive.
- Only physically backed DMA-buffer pages from registered allocations appear in PTEs. IOVA equals the physical pool address **but is translated by the IOMMU**, rather than bypassing it. No identity-map-all policy.
- Read/write permissions reflect DMA buffer direction. Permissions operate at page granularity, not byte granularity. The modern VirtIO data sector and completion status are separated into dedicated 4-KiB pages.
- Synchronous context-cache and IOTLB invalidations occur around attach/detach. A separate IOTLB-only invalidation is available for permission changes while the context stays alive. Timeouts fail closed.
- PCI bus mastering stays disabled until the domain has been attached. Detach resets the device, disables bus mastering, invalidates translation, then zeroes and releases buffers, without restoring bus mastering.

## Why the legacy path is insufficient
The VirtIO legacy PCI transport does **not** negotiate `VIRTIO_F_ACCESS_PLATFORM`. With Intel VT-d emulated and GSTS.TES=1, QEMU can still route that legacy device's DMA outside vIOMMU translation. An out-of-range DMA read therefore completed without an IOMMU fault: this is an observed **negative security result**, not evidence of isolation.

The VT-d preview instead uses `src/storage/virtio_blk_modern.h` (VirtIO 1.0 / PCI 1af4:1042), refusing binding unless `VERSION_1` and `ACCESS_PLATFORM` are both negotiated and FEATURES_OK is accepted. Device MMIO is in a 64-bit BAR above 4 GiB and is mapped into an uncached supervisor-only CPU VA window. It reads exclusively from a disposable, read-only FAT16 test image.

The kernel checks both VirtIO completion status **and** the IOMMU's fault status. QEMU may write the VirtIO used-ring completion even after rejecting DMA to a forbidden address. In this event, the kernel rejects the read instead of copying old bounce-buffer contents.

## Reproducing
From the repository root on Windows, with local Zig and Python 3.10:
```powershell
$env:ZIG_GLOBAL_CACHE_DIR="$PWD\tools\zig-cache"
& 'C:\Program Files\Python310\python.exe' .\scripts\build\make_proc_space_preview.py --experiment integration --vtd
& 'C:\Program Files\Python310\python.exe' .\tests\runners\test_vtd_hardware_fault.py fault
& 'C:\Program Files\Python310\python.exe' .\tests\runners\test_vtd_hardware_fault.py invalidate
```
Results: `build/vtd-hardware-fault-test.json`, `build/vtd-hardware-fault-serial.log`, `build/vtd-iotlb-invalidation-test.json`, `build/vtd-iotlb-invalidation-serial.log`. QEMU includes trace records for `vtd_dmar_translate` and `vtd_dmar_fault`. Only claim a given result if its JSON has `passed: true`.

## Not implemented; limitations
- No physical Intel silicon test or AMD-Vi.
- No multiple PCI segments/DRHD/RMRR, no ACPI hotplug, no PCI bridge path traversal, no queued invalidation or interrupt remapping, no ATS/PASID/SR-IOV.
- No multi-process concurrent DMA domains: code has bounded BDF-specific tables, but coherent DMA allocator is global and can only be claimed by one device at a time.
- Not SMP-safe, no general-purpose DMA API/MDL/scatter-gather, no 64-bit memory allocator.
- No safe recovery from a malicious CPL0 driver, MMIO spoofing, or malicious firmware; VT-d protects against unauthorized endpoint memory transactions, not arbitrary privileged CPU code.
- The boot path is fail-closed when required DMAR features/scopes are absent; hardware support is intentionally narrow and must not be promoted to production without expanded verification.

## Verified evidence (2026-10-22)
Run `.\scripts\build\verify_intel_vtd.ps1` to regenerate all results and enforce the stable-ISO and FAT16 hashes.
The completed QEMU 11.1.0 run returned `WINUX_VTD_HARDWARE_OK`, exit code 0, for all three independent VM modes:
- `positive`: four VirtIO 1.0 ACCESS_PLATFORM attach/read/detach/reprobe cycles; translated DMA seen in QEMU and **no** remapping faults.
- `fault`: unauthorized IOVA `0x08000000` rejected; the hardware DMAR fault record identified PCI `00:02.0`; guest read returned failure and the QEMU trace independently recorded `vtd_dmar_fault`.
- `invalidate`: after a successful DMA read, the data page write permission was revoked and synchronous IOTLB invalidation completed; an attempted repeat read faulted, then mapping was restored and the device reprobed.
Each mode has five regression checks, with `passed: true`. The legacy Foundation Preview separately passed 65 QEMU groups. No FAT16 test image or stable ISO was changed.

The QEMU fault is retrieved by **bounded polling after block I/O**, not a production ISR; MSI/MSI-X and queued invalidation are not implemented.
