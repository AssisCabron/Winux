/* Original, bounded PE32+ loader for the trusted bundled demo, not general Win32. */
typedef struct {
    const ram_file *file;
    u8 *base;
    u32 size, entry, optional, dirs, state, dll, attached;
} pe_module;

static pe_module modules[16];
static unsigned module_count;
static u8 *app_image;
static u32 app_size;
static u64 rd64(const u8 *p) { return rd32(p) | ((u64) rd32(p + 4) << 32); }

static int app_range(const void *p, u64 n) {
    u64 a = (u64) p;
    for (unsigned i = 0; i < module_count; i++) {
        u64 b = (u64) modules[i].base;
        if (a >= b && range(a - b, n, modules[i].size))return 1;
    }
    return 0;
}

static int stack_range(const void *p, u64 n) {
    u64 a = (u64) p, b = (u64) kernel_stack, c = (u64) app_stack;
    return (a >= b && range(a - b, n, sizeof(kernel_stack))) || (a >= c && range(a - c, n, sizeof(app_stack)));
}

static int case_same(const char *a, const char *b) {
    while (*a && *b) {
        char x = *a++, y = *b++;
        if (x >= 'A' && x <= 'Z')x += 32;
        if (y >= 'A' && y <= 'Z')y += 32;
        if (x != y)return 0;
    }
    return !*a && !*b;
}

static void *win_GetModuleHandleW(const u16 *name);

static void *win_GetProcAddress(void *module, const char *name);

__attribute__((used,noinline)) static void process_detach(void);

static int nt_native_dispose(void);

#include "../runtime/win_runtime.h"
#include "pe_tls.h"
#include "pe_modules.h"

static void pe_load(const ram_file *f, int execute) {
    /* Managed-only images can have no native entrypoint. Report the missing
     * execution engine before applying the native-entrypoint validator. */
    if (execute && f->size >= 64 && rd16(f->data) == 0x5a4d) {
        u64 pe = rd32(f->data + 0x3c), o = pe + 24;
        if (range(pe, 24, f->size) && rd32(f->data + pe) == 0x4550 &&
            rd16(f->data + pe + 20) >= 232 && range(o, 232, f->size) &&
            rd16(f->data + o) == 0x20b && rd32(f->data + o + 108) > 14 && rd32(f->data + o + 224)) {
            print("PE_BLOCKED: .NET CLR runtime is not implemented; entry not called.\n");
            return;
        }
    }
    if (!nt_native_dispose()) {
        print("PE_BLOCKED: native context cleanup failed; restart required\n");
        return;
    }
    module_count = init_count = 0;
    module_used = 0;
    graph_error = graph_running = 0;
    native_graph = same(f->name, "cmd.exe") && module_file("ntdll.dll") != 0;
    pe_module *main = module_load(f, 0);
    if (!main || graph_error) {
        print("PE_REJECTED: malformed image, imports, exports or relocations\n");
        return;
    }
    if (main->state != 2) {
        print("PE_BLOCKED: required APIs are not implemented; entry not called.\n");
        return;
    }
    app_image = main->base;
    app_size = main->size;
    if (!runtime_reset(f->name)) {
        print("PE_BLOCKED: process parameter initialization failed\n");
        return;
    }
    if (!prepare_static_tls()) {
        print("PE_REJECTED: invalid or oversized static TLS\n");
        return;
    }
    print("Mapped modules: ");
    number(module_count);
    print("; arena bytes: ");
    number(module_used);
    print("\n");
    if (!execute) {
        print("Dependency check complete. Entry not called.\n");
        return;
    }
    if (!same(f->name, "hello.exe") && !same(f->name, "runtime.exe") && !same(f->name, "dlltest.exe") && !
        same(f->name, "dllexit.exe") && !same(f->name, "tlstest.exe") && !same(f->name, "ntmemory.exe")) {
        print("PE_BLOCKED: execution currently limited to bundled original contract tests.\n");
        return;
    }
    for (unsigned i = 0; i < module_count; i++) {
        pe_module *m = &modules[i];
        u32 o = m->optional, d = m->dirs;
        if ((!m->dll && i) || (i == 0 && m->dll) || (!m->dll && rd16(m->base + o + 68) != 3) || (
                d > 13 && rd32(m->base + o + 216)) || (d > 14 && rd32(m->base + o + 224))) {
            print("PE_BLOCKED: unsupported module, subsystem, delay imports or CLR\n");
            return;
        }
    }
    u64 previous_gs = read_msr(0xc0000101);
    u16 previous_gs_selector, new_gs_selector = 16;
    __asm__ volatile("mov %%gs,%0":"=r"(previous_gs_selector));
    __asm__ volatile("mov %0,%%gs"::"r"(new_gs_selector):"memory");
    write_msr(0xc0000101, (u64) app_teb);
    /* Synchronize the instruction stream after writing executable pages and GS.
     * WRMSR to FS/GS base alone is not an instruction-stream serialization point. */
    __asm__ volatile("xor %%eax,%%eax; cpuid":::"rax","rbx","rcx","rdx","memory");
    u64 observed_self;
    __asm__ volatile("movq %%gs:0x30,%0":"=r"(observed_self)::"memory");
    if (observed_self != (u64) app_teb) {
        __asm__ volatile("mov %0,%%gs"::"r"(previous_gs_selector):"memory");
        write_msr(0xc0000101, previous_gs);
        serial("PE: invalid GS context before entry; observed/expected:\r\n");
        hex(observed_self);
        hex((u64) app_teb);
        print("PE_BLOCKED: thread context verification failed\n");
        return;
    }
    serial("PE: entering original Win32 contract test with GS context\r\n");
    static u8 fp_before[512] __attribute__((aligned(16))), fp_after[512] __attribute__((aligned(16)));
    __asm__ volatile("fxsave64 %0":"=m"(fp_before)::"memory");
    int result = app_enter((void *) start_module_graph);
    obj_reset(&nt_objects);
    for (unsigned i = 0; i < 16; i++)if (vm_regions[i].size)vm_release(vm_regions[i].base);
    __asm__ volatile("fxsave64 %0":"=m"(fp_after)::"memory");
    /* MXCSR status flags are volatile; x87 CW reserved bits can normalize. */
    if ((rd32(fp_before + 24) & 0xffc0) != (rd32(fp_after + 24) & 0xffc0) || (rd16(fp_before) & 0x1f3f) != (
            rd16(fp_after) & 0x1f3f)) {
        __asm__ volatile("fxrstor64 %0"::"m"(fp_before):"memory");
        serial("PE: FP control restore failed; MXCSR before/after, x87 before/after:\r\n");
        hex(rd32(fp_before + 24));
        hex(rd32(fp_after + 24));
        hex(rd16(fp_before));
        hex(rd16(fp_after));
        result = 95;
    } else serial("PE: FP control restored\r\n");
    __asm__ volatile("mov %0,%%gs"::"r"(previous_gs_selector):"memory");
    write_msr(0xc0000101, previous_gs);
    serial(read_msr(0xc0000101) == previous_gs ? "PE: GS restored\r\n" : "PE: GS restore failed\r\n");
    print("Program returned ");
    number((u32) result);
    print("\n");
    serial(result == 0 ? "PE_EXEC_OK\r\n" : "PE_EXEC_FAILED\r\n");
}

/* Mutate only the mapped original TLS fixture, never source files. */
static int tls_bounds_test(void) {
    const ram_file *f = find_file("tlstest.exe");
    if (!f)return 0;
    pe_load(f, 0);
    if (module_count != 1 || modules[0].state != 2)return 0;
    pe_module *m = &modules[0];
    u8 *t = m->base + rd32(m->base + m->optional + 184), saved[40];
    for (u32 i = 0; i < 40; i++)saved[i] = t[i];
    int ok = 1;
    for (u32 test = 0; test < 5; test++) {
        for (u32 i = 0; i < 40; i++)t[i] = saved[i];
        if (test == 0)*(u64 *) (t + 8) = rd64(t) - 1;
        if (test == 1)*(u64 *) (t + 16) = (u64) m->base + m->size;
        if (test == 2)*(u32 *) (t + 32) = 0xffffffff;
        if (test == 3)*(u64 *) (t + 24) = (u64) m->base + m->size - 4;
        if (test == 4)*(u32 *) (t + 36) = 0xf00000;
        if (prepare_static_tls())ok = 0;
    }
    for (u32 i = 0; i < 40; i++)t[i] = saved[i];
    return prepare_static_tls() && ok;
}
