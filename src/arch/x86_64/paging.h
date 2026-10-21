/* Original four-level paging for the current low-memory x64 VM configuration.
 * Low 4 GiB stays supervisor identity-mapped; a separate 256 MiB VA window
 * provides 4 KiB mappings. This is not user-mode isolation or a general VMM. */
#define PG_PRESENT 1ULL
#define PG_WRITE 2ULL
#define PG_USER 4ULL
#define PG_NX (1ULL<<63)
#define PG_MASK 0x000ffffffffff000ULL
#define PG_WINDOW 0x100000000ULL
#define PG_WINDOW_SIZE (256ULL*1024*1024)
static u64 pg_tables[64][512] __attribute__((aligned(4096)));
static u32 pg_used;
static u64 *pg_root;
static int pg_ready;
static void pg_zero(void *p, u64 bytes) { for (u64 i = 0; i < bytes; i++)((u8 *) p)[i] = 0; }

static u64 *pg_new(void) {
    if (pg_used == 64)return 0;
    u64 *p = pg_tables[pg_used++];
    pg_zero(p, 4096);
    return p;
}

static int pg_low(u64 a, u64 n) { return a < PG_WINDOW && n <= PG_WINDOW - a; }
static void pg_flush(u64 va) { __asm__ volatile("invlpg (%0)"::"r"(va):"memory"); }

static u64 *pg_leaf(u64 va, int create) {
    if (va < PG_WINDOW || va >= PG_WINDOW + PG_WINDOW_SIZE)return 0;
    u64 *table = pg_root;
    const u32 shifts[] = {39, 30, 21};
    for (u32 level = 0; level < 3; level++) {
        u64 *e = &table[(va >> shifts[level]) & 511];
        if (!(*e & PG_PRESENT)) {
            if (!create)return 0;
            u64 *p = pg_new();
            if (!p)return 0;
            *e = (u64) p | 3;
        }
        if (*e & 128)return 0;
        table = (u64 *) (*e & PG_MASK);
    }
    return &table[(va >> 12) & 511];
}

static int pg_map(u64 va, u64 pa, int writable, int executable) {
    if (!pg_ready || (va & 4095) || (pa & 4095) || !pg_low(pa, 4096))return 0;
    u64 *e = pg_leaf(va, 1);
    if (!e || (*e & 1))return 0;
    *e = pa | 1 | (writable ? 2 : 0) | (executable ? 0 : PG_NX);
    pg_flush(va);
    return 1;
}

static int pg_protect(u64 va, int writable, int executable) {
    if (!pg_ready || (va & 4095))return 0;
    u64 *e = pg_leaf(va, 0);
    if (!e || !(*e & 1))return 0;
    *e = (*e & PG_MASK) | 1 | (writable ? 2 : 0) | (executable ? 0 : PG_NX);
    pg_flush(va);
    return 1;
}

static int pg_unmap(u64 va) {
    if (!pg_ready || (va & 4095))return 0;
    u64 *e = pg_leaf(va, 0);
    if (!e || !(*e & 1))return 0;
    *e = 0;
    pg_flush(va);
    return 1;
}

#ifdef WINUX_RING3_PREVIEW
/* User mappings share the preview address space, while every kernel/identity
 * leaf remains supervisor-only. This is NOT per-process CR3 isolation. */
static u64 *pg_user_leaf(u64 va, int create) {
    if(va<PG_WINDOW || va>=PG_WINDOW+PG_WINDOW_SIZE)return 0;
    u64 *table=pg_root;
    const unsigned shifts[]={39,30,21};
    for(unsigned level=0;level<3;level++){
        u64 *entry=&table[(va>>shifts[level])&511];
        if(!(*entry&PG_PRESENT)){
            if(!create)return 0;
            u64 *next=pg_new();
            if(!next)return 0;
            *entry=(u64)next|PG_PRESENT|PG_WRITE|PG_USER;
        }else{
            if(*entry&128ULL)return 0;
            *entry|=PG_USER;
        }
        table=(u64*)(*entry&PG_MASK);
    }
    return &table[(va>>12)&511];
}
static int pg_map_user(u64 va,u64 pa,int writable,int executable) {
    if(!pg_ready||(va&4095)||(pa&4095)||!pg_low(pa,4096)|| (writable&&executable))return 0;
    u64 *e=pg_user_leaf(va,1);
    if(!e||(*e&PG_PRESENT))return 0;
    *e=pa|PG_PRESENT|PG_USER|(writable?PG_WRITE:0)|(executable?0:PG_NX);
    pg_flush(va);return 1;
}
static int pg_protect_user(u64 va,int writable,int executable) {
    if(!pg_ready||(va&4095)||(writable&&executable))return 0;
    u64 *e=pg_user_leaf(va,0);
    if(!e||((*e&(PG_PRESENT|PG_USER))!=(PG_PRESENT|PG_USER)))return 0;
    *e=(*e&PG_MASK)|PG_PRESENT|PG_USER|(writable?PG_WRITE:0)|(executable?0:PG_NX);
    pg_flush(va);return 1;
}
#endif

/* One fixed shared-data page for validated local NTDLL syscall stubs only.
 * Split its identity-mapped 2 MiB leaf; neighboring mappings stay unchanged. */
static int pg_shared_page(void *backing) {
    const u64 va = 0x7ffe0000;
    if (!pg_ready || ((u64) backing & 4095) || !pg_low((u64) backing, 4096))return 0;
    if ((va < (u64) boot->image_base + boot->image_size && va + 4096 > (u64) boot->image_base) ||
        (va < (u64) boot->app_arena + boot->app_arena_size && va + 4096 > (u64) boot->app_arena))
        return 0;
    u64 *pdpt = (u64 *) (pg_root[0] & PG_MASK), *pd = (u64 *) (pdpt[va >> 30] & PG_MASK), *e = &pd[(va >> 21) & 511];
    if (!(*e & 128))return 0;
    u64 *pt = pg_new();
    if (!pt)return 0;
    u64 physical = *e & 0x000fffffffe00000ULL, flags = *e & 0x1f;
    for (unsigned i = 0; i < 512; i++)pt[i] = (physical + (u64) i * 4096) | flags;
    pt[(va >> 12) & 511] = (u64) backing | PG_PRESENT | PG_NX;
    *e = (u64) pt | 3;
    pg_flush(va);
    return 1;
}

static int paging_init(void) {
    u64 cr4;
    __asm__ volatile("mov %%cr4,%0":"=r"(cr4));
    if (cr4 & (1ULL << 12))return 0; /* LA57 unsupported */
    u32 a, b, c, d;
    __asm__ volatile("cpuid":"=a"(a),"=b"(b),"=c"(c),"=d"(d):"a"(0x80000000),"c"(0));
    if (a < 0x80000001)return 0;
    __asm__ volatile("cpuid":"=a"(a),"=b"(b),"=c"(c),"=d"(d):"a"(0x80000001),"c"(0));
    if (!(d & (1U << 20)))return 0;
    if (!boot->image_base || !boot->image_size || !pg_low((u64) boot->image_base, boot->image_size) ||
        !pg_low((u64) boot, sizeof(*boot)) || !pg_low((u64) boot->memory_map, boot->map_size) ||
        !pg_low((u64) boot->app_arena, boot->app_arena_size) || !pg_low((u64) pg_tables, sizeof(pg_tables)) ||
        !pg_low((u64) kernel_stack, sizeof(kernel_stack)) || !pg_low((u64) app_stack, sizeof(app_stack)) ||
        (boot->framebuffer_size && !pg_low(boot->framebuffer, boot->framebuffer_size)))
        return 0;
    pg_used = 0;
    pg_root = pg_new();
    u64 *pdpt = pg_new();
    pg_root[0] = (u64) pdpt | 3;
    for (u32 g = 0; g < 4; g++) {
        u64 *pd = pg_new();
        pdpt[g] = (u64) pd | 3;
        for (u32 i = 0; i < 512; i++) {
            u64 pa = ((u64) g << 30) | ((u64) i << 21), flags = 0x83;
            /* Framebuffer mapped uncached; no promise of renderer performance. */
            if (boot->framebuffer_size && pa < boot->framebuffer + boot->framebuffer_size && boot->framebuffer < pa +
                0x200000)flags |= 0x18;
            pd[i] = pa | flags;
        }
    }
    __asm__ volatile("rdmsr":"=a"(a),"=d"(d):"c"(0xc0000080));
    a |= 0x800;
    __asm__ volatile("wrmsr"::"a"(a),"d"(d),"c"(0xc0000080):"memory");
    u64 clear_global = cr4 & ~0x80ULL;
    __asm__ volatile("mov %0,%%cr4; mov %1,%%cr3; mov %2,%%cr4"::"r"(clear_global),"r"((u64) pg_root),"r"(cr4):"memory")
            ;
    u64 cr0;
    __asm__ volatile("mov %%cr0,%0":"=r"(cr0));
    cr0 |= 1ULL << 16;
    __asm__ volatile("mov %0,%%cr0"::"r"(cr0):"memory");
    u64 current;
    __asm__ volatile("mov %%cr3,%0":"=r"(current));
    pg_ready = (current & PG_MASK) == (u64) pg_root;
    return pg_ready;
}
