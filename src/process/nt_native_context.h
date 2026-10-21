/* Single-thread, pinned-NTDLL context lifetime. No DLL entry points.
 * Context owns the process heap; callers own allocations until dispose.
 * Module arena must remain mapped until nt_native_dispose succeeds. */
typedef struct {
    u64 gs;
    u16 selector;
    int allowed;
} nt_native_scope;

static struct {
    pe_module *module;
    void *heap;
    unsigned locks;
    int active;
} nt_native;

static void nt_native_enter(nt_native_scope *s) {
    s->gs = read_msr(0xc0000101);
    s->allowed = nt_syscall_allowed;
    __asm__ volatile("mov %%gs,%0":"=r"(s->selector));
    u16 selector = 16;
    __asm__ volatile("mov %0,%%gs"::"r"(selector):"memory");
    write_msr(0xc0000101, (u64) app_teb);
    __asm__ volatile("xor %%eax,%%eax; cpuid":::"rax","rbx","rcx","rdx","memory");
    nt_syscall_allowed = 1;
}

static void nt_native_leave(const nt_native_scope *s) {
    nt_syscall_allowed = s->allowed;
    __asm__ volatile("mov %0,%%gs"::"r"(s->selector):"memory");
    write_msr(0xc0000101, s->gs);
}

static int nt_native_dispose(void) {
    if (!nt_native.module)return 1;
    pe_module *m = nt_native.module;
    /* Never overwrite the arena after an unverified/partial teardown. */
    if (m->state != 2)return 0;
    u64 destroy = module_export(m, "RtlDestroyHeap", 0, 0), remove = module_export(m, "RtlDeleteCriticalSection", 0, 0),
            depth = module_export(m, "RtlQueryDepthSList", 0, 0);
    if (!destroy || !remove || !depth)return 0;
    nt_native_scope scope;
    nt_native_enter(&scope);
    int ok = 0;
    if (nt_native.heap) {
        if (rd64(app_peb + 0x30) != (u64) nt_native.heap)goto done;
        /* A dependent DLL owns its private heaps. Refuse BEFORE callbacks or any
         * destruction; its detach may still need the process heap and FLS data. */
        if (rd32(app_peb + 0xe8) != 1 || rd64(app_peb + 0xf0) != (u64) m->base + 0x16ad40 ||
            rd64(m->base + 0x16ad40) != (u64) nt_native.heap)
            goto done;
        u64 fls = rd64(app_teb + 0x17c8);
        if (fls) {
            u64 cleanup = module_export(m, "RtlProcessFlsData", 0, 0);
            if (!cleanup || !nt_obj_readable((void *) fls, 0x50))goto done;
            /* Mask 1 runs callbacks/unlinks; mask 2 frees per-thread tables and data.
             * Keep the process heap and mapped callback code alive throughout. */
            ((void(*)(void *, u32)) cleanup)((void *) fls, 3);
            pp_put64(app_teb + 0x17c8, 0);
        }
        if (rd64(m->base + 0x16d338) != (u64) m->base + 0x16d338 || rd64(m->base + 0x16d340) != (u64) m->base +
            0x16d338)goto done;
        /* A callback can create a heap; never destroy the process heap under it. */
        if (rd32(app_peb + 0xe8) != 1 || rd64(m->base + 0x16ad40) != (u64) nt_native.heap)goto done;
        if (!kb_inputs_stop() || !kb_nls_stop())goto done;
        pp_put64(app_peb + 0x30, 0);
        if (((void*(*)(void *)) destroy)(nt_native.heap)) {
            pp_put64(app_peb + 0x30, (u64) nt_native.heap);
            goto done;
        }
        nt_native.heap = 0;
        nt_native.active = 0;
        pg_zero(m->base + 0x16d2f0, 0x60); /* Slot tables belonged to the destroyed heap. */
        pp_put64(m->base + 0x16d338, (u64) m->base + 0x16d338);
        pp_put64(m->base + 0x16d340, (u64) m->base + 0x16d338);
    }
    if (rd32(app_peb + 0xe8))goto done; /* Other heaps still belong to this context. */
    if (nt_native.locks & 2) {
        if (((u32(*)(void *)) remove)(m->base + 0x16ace0))goto done;
        nt_native.locks &= ~2U;
    }
    if (nt_native.locks & 1) {
        if (((u32(*)(void *)) remove)(m->base + 0x16c0e0))goto done;
        nt_native.locks &= ~1U;
    }
    if (((u16(*)(void *)) depth)(m->base + 0x16d350) != 8)goto done;
    pp_put64(app_peb + 0x38, 0);
    pp_put32(app_peb + 0xec, 0);
    pp_put64(app_peb + 0xf0, 0);
    pp_put64(app_peb + 0xa0, 0);
    pp_put64(app_peb + 0xa8, 0);
    pp_put64(app_peb + 0xb0, 0);
    nt_console_stop();
    nt_native.module = 0;
    ok = 1;
done:
    nt_native_leave(&scope);
    return ok;
}

static int nt_native_start(pe_module *m) {
    if (nt_native.module || rd64(app_peb + 0x30) || rd64(app_teb + 0x17c8) || !m || m->state != 2 || m->size < 0x16db48
        || !nt_syscall_init())return 0;
    const char *names[] = {
        "RtlInitializeSListHead", "RtlInterlockedPushListSList", "RtlInitializeCriticalSectionEx", "RtlCreateHeap"
    };
    u64 fn[4];
    for (unsigned i = 0; i < 4; i++) {
        fn[i] = module_export(m, names[i], 0, 0);
        if (!fn[i])return 0;
    }
    u8 *list = m->base + 0x166640, *pool = m->base + 0x16ae40, *head = m->base + 0x16d350;
    if (rd64(list) != (u64) list || rd64(list + 8) != (u64) list || rd64(m->base + 0x16d4d8) ||
        rd64(m->base + 0x16ac28))return 0;
    if (!nt_nls_setup(m))return 0;
    /* Producer observed in the version-bound process bootstrap. */
    pg_zero(m->base + 0x16d2f0, 0x60);
    pp_put64(m->base + 0x16d338, (u64) m->base + 0x16d338);
    pp_put64(m->base + 0x16d340, (u64) m->base + 0x16d338);
    ((void(*)(void *)) fn[0])(head);
    for (unsigned i = 0; i < 8; i++)pp_put64(pool + i * 0x30, i == 7 ? 0 : (u64)(pool + (i + 1) * 0x30));
    ((void*(*)(void *, void *, void *, u32)) fn[1])(head, pool, pool + 7 * 0x30, 8);
    pp_put64(app_peb + 0xc8, 0x100000);
    pp_put64(app_peb + 0xd0, 0x2000);
    pp_put64(app_peb + 0xd8, 0x10000);
    pp_put64(app_peb + 0xe0, 0x1000);
    pp_put32(app_peb + 0xe8, 0);
    pp_put32(app_peb + 0xec, 16);
    pp_put64(app_peb + 0xf0, (u64) m->base + 0x16ad40);
    pp_put64(app_peb + 0xa0, (u64) find_file("c_1252.nls")->data);
    pp_put64(app_peb + 0xa8, (u64) find_file("c_850.nls")->data);
    pp_put64(app_peb + 0xb0, (u64) find_file("l_intl.nls")->data);
    nt_native.module = m;
    nt_native_scope scope;
    nt_native_enter(&scope);
    int ok = 0;
    typedef u32 (*init_t)(void *, u32, u32);
    if (((init_t) fn[2])(m->base + 0x16c0e0, 0, 0x10000000))goto done;
    nt_native.locks |= 1;
    pp_put64(app_peb + 0x38, (u64) m->base + 0x16c0e0);
    if (((init_t) fn[2])(m->base + 0x16ace0, 0, 0x10000000))goto done;
    nt_native.locks |= 2;
    nt_native.heap = ((void*(*)(u32, void *, u64, u64, void *, void *)) fn[3])(2, 0, 0x100000, 0x1000, 0, 0);
    if (!nt_native.heap)goto done;
    pp_put64(app_peb + 0x30, (u64) nt_native.heap);
    if (rd32(app_peb + 0xe8) != 1 || rd64(m->base + 0x16ad40) != (u64) nt_native.heap)goto done;
    if (!kb_nls_start() || !kb_inputs_start(m) || !nt_console_start())goto done;
    nt_native.active = 1;
    ok = 1;
done:
    nt_native_leave(&scope);
    if (!ok)nt_native_dispose();
    return ok;
}
