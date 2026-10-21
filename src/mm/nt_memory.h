/* Original NT-signature adapters for trusted, single-process PE fixtures.
 * Direct import bindings only: no SYSCALL transition or Microsoft code here.
 * Unsupported allocation policies return STATUS_NOT_SUPPORTED explicitly. */
#define NT_INVALID_PARAMETER 0xc000000dU
#define NT_NOT_SUPPORTED 0xc00000bbU
static unsigned nt_protection(u32 p) { return p == 2 ? 1 : p == 4 ? 3 : p == 0x20 ? 5 : p == 0x40 ? 7 : 0; }

static int nt_span(u64 address, u64 bytes, u64 *base, u64 *length) {
    if (!bytes || bytes > ~0ULL - address || address + bytes > ~0ULL - 4095)return 0;
    *base = address & ~4095ULL;
    *length = ((address + bytes + 4095) & ~4095ULL) - *base;
    return 1;
}

static u32 nt_AllocateVirtualMemory(void *process, void **address, u64 zero_bits, u64 *bytes, u32 type,
                                    u32 protection) {
    if (process != (void *) ~0ULL)return 0xc0000008U;
    if (!win_buffer(address, 8) || !win_buffer(bytes, 8))return 0xc0000005U;
    if (zero_bits || !(type == 0x1000 || type == 0x2000 || type == 0x3000) || !nt_protection(protection))return
            NT_NOT_SUPPORTED;
    u64 base = (u64) * address, length;
    int created = 0;
    if (!*bytes)return NT_INVALID_PARAMETER;
    if (!base) {
        if (*bytes > VM_REGION_PAGES * 4096)return 0xc0000017U;
        length = (*bytes + 4095) & ~4095ULL;
        base = vm_reserve(length);
        if (!base)return 0xc0000017U;
        vm_find(base, 1)->allocation_protect = protection;
        created = 1;
    } else {
        if (type & 0x2000)return NT_NOT_SUPPORTED; /* Requested-address reservation not implemented. */
        if (!nt_span(base, *bytes, &base, &length))return NT_INVALID_PARAMETER;
        if (!vm_find(base, length))return 0xc0000018U;
    }
    if ((type & 0x1000) && !vm_commit(base, length, nt_protection(protection))) {
        if (created)vm_release(base);
        return 0xc0000017U;
    }
    *address = (void *) base;
    *bytes = length;
    return 0;
}

static u32 nt_ProtectVirtualMemory(void *process, void **address, u64 *bytes, u32 protection, u32 *old) {
    if (process != (void *) ~0ULL)return 0xc0000008U;
    if (!win_buffer(address, 8) || !win_buffer(bytes, 8) || !win_buffer(old, 4))return 0xc0000005U;
    if (!nt_protection(protection))return NT_NOT_SUPPORTED;
    u64 base, length;
    if (!nt_span((u64) * address, *bytes, &base, &length))return NT_INVALID_PARAMETER;
    vm_region *r = vm_find(base, length);
    if (!r)return 0xc0000018U;
    *old = 1; /* Observed output on STATUS_NOT_COMMITTED for this local NT build. */
    u32 prior = vm_windows_protect(r->protection[(base - r->base) / 4096]);
    if (!vm_protect(base, length, nt_protection(protection)))return 0xc000002dU;
    *old = prior;
    *address = (void *) base;
    *bytes = length;
    return 0;
}

static u32 nt_FreeVirtualMemory(void *process, void **address, u64 *bytes, u32 type) {
    if (process != (void *) ~0ULL)return 0xc0000008U;
    if (!win_buffer(address, 8) || !win_buffer(bytes, 8))return 0xc0000005U;
    if (type != 0x4000 && type != 0x8000)return 0xc00000f2U;
    u64 base = (u64) * address, length = *bytes;
    vm_region *r = vm_find(base, 1);
    if (!r)return NT_INVALID_PARAMETER;
    if (type == 0x8000) {
        if (!length) {
            if (base != r->base)return NT_NOT_SUPPORTED;
            length = r->size;
            if (!vm_release(base))return NT_INVALID_PARAMETER;
        } else {
            if (!nt_span(base, length, &base, &length) || !vm_find(base, length))return NT_INVALID_PARAMETER;
            if (!vm_release_span(base, length))return 0xc0000017U;
        }
    } else {
        if (!length) {
            if (base != r->base)return NT_NOT_SUPPORTED;
            length = r->size;
        } else if (!nt_span(base, length, &base, &length))return NT_INVALID_PARAMETER;
        if (!vm_decommit(base, length))return NT_INVALID_PARAMETER;
    }
    *address = (void *) base;
    *bytes = length;
    return 0;
}

static u32 nt_QueryVirtualMemory(void *process, void *address, u32 info_class, void *buffer, u64 length,
                                 u64 *returned) {
    if (process != (void *) ~0ULL)return 0xc0000008U;
    if (info_class == 6) {
        /* MemoryImageInformation for the current loader's mapped images.
         * No signature/partial-map flags are claimed by this experimental loader. */
        if (length < 24)return 0xc0000004U;
        if (!nt_obj_writable(buffer, 24) || (returned && !nt_obj_writable(returned, 8)))return 0xc0000005U;
        u64 base = 0, size = 0, a = (u64) address;
        for (unsigned i = 0; i < module_count; i++) {
            pe_module *m = &modules[i];
            u64 b = (u64) m->base;
            if (m->state == 2 && a >= b && a - b < m->size) {
                base = b;
                size = m->size;
                break;
            }
        }
        if (!base && !nt_obj_readable(address, 1) && !vm_find(a, 1))return 0xc0000141U;
        /* Validate all outputs before publishing; failures leave both unchanged. */
        u8 *p = buffer;
        for (unsigned i = 0; i < 24; i++)p[i] = 0;
        for (unsigned i = 0; i < 8; i++) {
            p[i] = (u8)(base >> (8 * i));
            p[8 + i] = (u8)(size >> (8 * i));
        }
        if (returned)*returned = 24;
        return 0;
    }
    if (info_class)return NT_NOT_SUPPORTED;
    if (length < sizeof(vm_basic_info))return 0xc0000004U;
    if (!nt_obj_writable(buffer, sizeof(vm_basic_info)) || (returned && !nt_obj_writable(returned, 8)))return
            0xc0000005U;
    vm_basic_info info;
    if (!vm_query((u64) address, &info))return NT_NOT_SUPPORTED;
    *(vm_basic_info *) buffer = info;
    if (returned)*returned = sizeof(info);
    return 0;
}

static u64 nt_memory_export(const char *name) {
    if (same(name, "NtAllocateVirtualMemory"))return (u64) nt_AllocateVirtualMemory;
    if (same(name, "NtProtectVirtualMemory"))return (u64) nt_ProtectVirtualMemory;
    if (same(name, "NtFreeVirtualMemory"))return (u64) nt_FreeVirtualMemory;
    if (same(name, "NtQueryVirtualMemory"))return (u64) nt_QueryVirtualMemory;
    return 0;
}
