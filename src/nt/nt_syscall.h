/* Experimental CPL0-to-CPL0 SYSCALL path, one CPU, no nesting or user mode.
 * The CPU does not switch RSP. Save the entry frame and use our own stack.
 * Returning via JMP deliberately stays at CPL0; SYSRET to ring 3 is not built. */
#include "../../abi/generated/nt_services_19041.h"
u8 nt_syscall_stack[65536] __attribute__((aligned(16)));
u64 nt_syscall_frame[8]; /* service, R10, RDX, R8, R9, RSP, RCX, R11 */
static u8 nt_shared_data[4096] __attribute__((aligned(4096)));
static int nt_syscall_ready, nt_syscall_allowed, nt_syscall_stack_ok;
static u32 nt_syscall_counts[8];

/* Bounded diagnostic ledger: only unknown NT service IDs, never call arguments.
 * The existing kernel is one-CPU/single-context; no SMP locking is claimed. */
#define NT_MISSING_SLOTS 16
static struct {u32 id,calls;} nt_missing_services[NT_MISSING_SLOTS];
static u32 nt_missing_used,nt_missing_overflow;
static void nt_missing_reset(void){
    nt_missing_used=nt_missing_overflow=0;
    for(unsigned i=0;i<NT_MISSING_SLOTS;i++){nt_missing_services[i].id=0;nt_missing_services[i].calls=0;}
}
static void nt_missing_note(u32 service){
    for(unsigned i=0;i<nt_missing_used;i++){
        if(nt_missing_services[i].id!=service)continue;
        if(nt_missing_services[i].calls!=0xffffffffU)nt_missing_services[i].calls++;
        return;
    }
    if(nt_missing_used==NT_MISSING_SLOTS){
        if(nt_missing_overflow!=0xffffffffU)nt_missing_overflow++;
        return;
    }
    nt_missing_services[nt_missing_used].id=service;
    nt_missing_services[nt_missing_used].calls=1;
    nt_missing_used++;
}
static void nt_missing_report(void){
    print("NT_MISSING_BEGIN unique=");number(nt_missing_used);
    print(" overflow=");number(nt_missing_overflow);print("\n");
    for(unsigned i=0;i<nt_missing_used;i++){
        print("NT_MISSING_SERVICE ");number(nt_missing_services[i].id);
        print(" calls=");number(nt_missing_services[i].calls);print("\n");
    }
    print("NT_MISSING_END\n");
}

__attribute__((used,noinline)) static u32 nt_dispatch(u64 *f) {
    u64 sp;
    __asm__ volatile("mov %%rsp,%0":"=r"(sp));
    if (sp < (u64) nt_syscall_stack || sp >= (u64) nt_syscall_stack + sizeof(nt_syscall_stack))return 0xc0000001U;
    nt_syscall_stack_ok = 1;
    if (!nt_syscall_allowed)return 0xc000001cU;
    /* Stack arguments belong to the original caller, not this dispatch stack. */
    u64 *args = (u64 *) f[5];
    u64 extra = wx_nt_stack_span(f[0]);
    if (!stack_range(args, extra))return 0xc0000005U;
    switch ((u32) f[0]) {
        case WX_NT_READ_FILE: return nt_ReadFile((void *) f[1], (void *) f[2], (void *) f[3], (void *) f[4], (void *) args[5],
                                   (void *) args[6], (u32) args[7], (const int64_t *) args[8], (const u32 *) args[9]);
        case WX_NT_WRITE_FILE: return nt_WriteFile((void *) f[1], (void *) f[2], (void *) f[3], (void *) f[4], (void *) args[5],
                                    (void *) args[6], (u32) args[7], (const int64_t *) args[8], (const u32 *) args[9]);
        case WX_NT_QUERY_VOLUME_INFORMATION_FILE: return nt_QueryVolumeInformationFile((void *) f[1], (void *) f[2], (void *) f[3], (u32) f[4],
                                                      (u32) args[5]);
        case WX_NT_CREATE_EVENT: nt_syscall_counts[7]++;
            return nt_CreateEvent((void **) f[1], (u32) f[2], (nt_obj_attributes *) f[3], (u32) f[4], (u8) args[5]);
        case WX_NT_SET_EVENT: nt_syscall_counts[7]++;
            return nt_SetEvent((void *) f[1], (int32_t *) f[2]);
        case WX_NT_WAIT_FOR_SINGLE_OBJECT: nt_syscall_counts[7]++;
            return nt_WaitForSingleObject((void *) f[1], (u8) f[2], (const int64_t *) f[3]);
        case WX_NT_CLOSE: nt_syscall_counts[7]++;
            return nt_Close((void *) f[1]);
        case WX_NT_OPEN_DIRECTORY_OBJECT: nt_syscall_counts[7]++;
            return nt_OpenDirectoryObject((void **) f[1], (u32) f[2], (nt_obj_attributes *) f[3]);
        case WX_NT_OPEN_SYMBOLIC_LINK_OBJECT: nt_syscall_counts[7]++;
            return nt_OpenSymbolicLinkObject((void **) f[1], (u32) f[2], (nt_obj_attributes *) f[3]);
        case WX_NT_QUERY_SYMBOLIC_LINK_OBJECT: nt_syscall_counts[7]++;
            return nt_QuerySymbolicLinkObject((void *) f[1], (nt_obj_string *) f[2], (u32 *) f[3]);
        case WX_NT_ALLOCATE_VIRTUAL_MEMORY: nt_syscall_counts[0]++;
            return nt_AllocateVirtualMemory((void *) f[1], (void **) f[2], f[3], (u64 *) f[4], (u32) args[5],
                                            (u32) args[6]);
        case WX_NT_FREE_VIRTUAL_MEMORY: nt_syscall_counts[1]++;
            return nt_FreeVirtualMemory((void *) f[1], (void **) f[2], (u64 *) f[3], (u32) f[4]);
        case WX_NT_QUERY_VIRTUAL_MEMORY: nt_syscall_counts[2]++;
            return nt_QueryVirtualMemory((void *) f[1], (void *) f[2], (u32) f[3], (void *) f[4], args[5],
                                         (u64 *) args[6]);
        case WX_NT_PROTECT_VIRTUAL_MEMORY: nt_syscall_counts[3]++;
            return nt_ProtectVirtualMemory((void *) f[1], (void **) f[2], (u64 *) f[3], (u32) f[4], (u32 *) args[5]);
        case WX_NT_QUERY_INFORMATION_PROCESS: nt_syscall_counts[5]++;
            return nt_QueryInformationProcess((void *) f[1], (u32) f[2], (void *) f[3], (u32) f[4], (u32 *) args[5]);
        case WX_NT_QUERY_SYSTEM_INFORMATION: nt_syscall_counts[6]++;
            return nt_QuerySystemInformation((u32) f[1], (void *) f[2], (u32) f[3], (u32 *) f[4]);
        default: nt_syscall_counts[4]++;
            nt_missing_note((u32)f[0]);
            return 0xc000001cU;
    }
}

__attribute__((naked)) static void nt_syscall_entry(void) {
    __asm__ volatile(
        "mov %rax,nt_syscall_frame(%rip); mov %r10,nt_syscall_frame+8(%rip);"
        "mov %rdx,nt_syscall_frame+16(%rip); mov %r8,nt_syscall_frame+24(%rip); mov %r9,nt_syscall_frame+32(%rip);"
        "mov %rsp,nt_syscall_frame+40(%rip); mov %rcx,nt_syscall_frame+48(%rip); mov %r11,nt_syscall_frame+56(%rip);"
        "lea nt_syscall_stack+65536(%rip),%rsp; sub $32,%rsp; lea nt_syscall_frame(%rip),%rcx; call nt_dispatch;"
        "mov nt_syscall_frame+48(%rip),%rcx; mov nt_syscall_frame+56(%rip),%r11; mov nt_syscall_frame+40(%rip),%rsp;"
        "push %r11; popfq; jmp *%rcx");
}

static int nt_syscall_init(void) {
    if (nt_syscall_ready)return 1;
    u32 a, b, c, d;
    __asm__ volatile("cpuid":"=a"(a),"=b"(b),"=c"(c),"=d"(d):"a"(0x80000001),"c"(0));
    if (!(d & (1U << 11)))return 0;
    /* Only the stub selector at +0x308 is implemented; this is not full KUSER_SHARED_DATA. */
    pg_zero(nt_shared_data, sizeof(nt_shared_data));
    if (!pg_shared_page(nt_shared_data))return 0;
    write_msr(0xc0000081, 8ULL << 32); /* STAR kernel CS8/SS16 */
    write_msr(0xc0000082, (u64) nt_syscall_entry);
    write_msr(0xc0000084, 0x40700); /* Mask TF, IF, DF, AC before any C code. */
    write_msr(0xc0000080, read_msr(0xc0000080) | 1);
    nt_syscall_ready = 1;
    return 1;
}

static u64 nt_checked_stub(pe_module *m, const char *name, u32 service) {
    u64 address = module_export(m, name, 0, 0);
    if (!address || address < (u64) m->base || !range(address - (u64) m->base, 24, m->size))return 0;
    const u8 *p = (const u8 *) address;
    const u8 prefix[] = {0x4c, 0x8b, 0xd1, 0xb8}, tail[] = {
        0xf6, 0x04, 0x25, 0x08, 0x03, 0xfe, 0x7f, 0x01, 0x75, 0x03, 0x0f, 0x05, 0xc3, 0xcd, 0x2e, 0xc3
    };
    for (unsigned i = 0; i < 4; i++)if (p[i] != prefix[i])return 0;
    if (rd32(p + 4) != service)return 0;
    for (unsigned i = 0; i < 16; i++)if (p[i + 8] != tail[i])return 0;
    return address;
}

__attribute__((naked)) static u32 nt_unknown_stub(void) {
    __asm__ volatile("mov %rcx,%r10; mov $0xffffffff,%eax; syscall; ret");
}

static int nt_stub_test(void) {
    const ram_file *cmd = find_file("cmd.exe");
    if (!cmd || !find_file("ntdll.dll"))return 0;
    pe_load(cmd, 0);
    pe_module *m = module_named("ntdll.dll");
    if (!m || m->state != 2)return 0;
    typedef u32 (*allocate_t)(void *, void **, u64, u64 *, u32, u32);
    typedef u32 (*free_t)(void *, void **, u64 *, u32);
    typedef u32 (*protect_t)(void *, void **, u64 *, u32, u32 *);
    typedef u32 (*query_t)(void *, void *, u32, void *, u64, u64 *);
    allocate_t allocate = (allocate_t) nt_checked_stub(m, "NtAllocateVirtualMemory", WX_NT_ALLOCATE_VIRTUAL_MEMORY);
    free_t release = (free_t) nt_checked_stub(m, "NtFreeVirtualMemory", WX_NT_FREE_VIRTUAL_MEMORY);
    protect_t protect = (protect_t) nt_checked_stub(m, "NtProtectVirtualMemory", WX_NT_PROTECT_VIRTUAL_MEMORY);
    query_t query = (query_t) nt_checked_stub(m, "NtQueryVirtualMemory", WX_NT_QUERY_VIRTUAL_MEMORY);
    if (!allocate || !release || !protect || !query || !nt_syscall_init())return 0;
    pg_zero(nt_syscall_counts, sizeof(nt_syscall_counts));
    nt_syscall_stack_ok = 0;
    nt_syscall_allowed = 1;
    void *base = 0, *address;
    u64 bytes = 8192, returned = 0;
    u32 old = 0;
    vm_basic_info info;
    int ok = 1;
    if (allocate((void *) ~0ULL, &base, 0, &bytes, 0x2000, 4) || !base) {
        ok = 0;
        goto done;
    }
    address = base;
    if (allocate((void *) ~0ULL, &address, 0, &bytes, 0x1000, 4)) {
        ok = 0;
        goto done;
    }
    volatile u8 *p = base;
    if (*p)ok = 0;
    *p = 93;
    if (query((void *) ~0ULL, base, 0, &info, sizeof(info), &returned) || returned != 48 || info.region_size != 8192 ||
        info.state != 0x1000 || info.protect != 4)ok = 0;
    address = base;
    bytes = 4096;
    if (protect((void *) ~0ULL, &address, &bytes, 2, &old) || old != 4 || *p != 93)ok = 0;
    if (query((void *) ~0ULL, base, 0, &info, sizeof(info), &returned) || info.region_size != 4096 || info.protect != 2
        || (*pg_leaf((u64) base, 0) & PG_WRITE))ok = 0;
done:
    if (base) {
        address = base;
        bytes = 0;
        if (release((void *) ~0ULL, &address, &bytes, 0x8000) || bytes != 8192) {
            ok = 0;
            vm_release((u64) base);
        }
    }
    if (nt_unknown_stub() != 0xc000001cU)ok = 0;
    nt_syscall_allowed = 0;
    if (!nt_syscall_stack_ok || nt_syscall_counts[0] != 2 || nt_syscall_counts[1] != 1 || nt_syscall_counts[2] != 2 ||
        nt_syscall_counts[3] != 1 || nt_syscall_counts[4] != 1)ok = 0;
    if (!pool_test())ok = 0;
    if (ok)print(
        "NT_STUB_EVIDENCE: original NTDLL bytes; 6 memory SYSCALLs; unknown service rejected; dedicated stack\n");
    return ok;
}
