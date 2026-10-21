/* Original limited process/system query adapters for the experimental guest. */
typedef struct {
    u32 reserved, timer, page_size, physical_pages, lowest_page, highest_page, granularity, padding;
    u64 minimum, maximum, affinity;
    u8 processors, tail[7];
} nt_system_basic;

_Static_assert (
sizeof
(nt_system_basic)
==
64
,
"system basic x64 size"
);
_Static_assert (
__builtin_offsetof
(nt_system_basic
,
maximum
)
==
40
,
"heap consumes maximum at +0x28"
);

static u32 nt_cookie(void) {
    if (!process_cookie) {
        u32 a, b, c, d, value = 0;
        u8 valid = 0;
        __asm__ volatile("cpuid":"=a"(a),"=b"(b),"=c"(c),"=d"(d):"a"(1),"c"(0));
        if (c & (1U << 30))for (unsigned i = 0; i < 10; i++) {
            __asm__ volatile("rdrand %0; setc %1":"=r"(value),"=qm"(valid));
            if (valid && value)break;
        }
        if (!valid || !value) {
            /* Functional fallback only: not a cryptographic entropy source. */
            static u32 generation;
            __asm__ volatile("rdtsc":"=a"(a),"=d"(d));
            value = a ^ d ^ (++generation * 0x9e3779b9U);
            value ^= value << 13;
            value ^= value >> 17;
            value ^= value << 5;
        }
        process_cookie = value ? value : 1;
    }
    return process_cookie;
}

static u32 nt_QueryInformationProcess(void *process, u32 info_class, void *buffer, u32 length, u32 *returned) {
    if (info_class != 0x24)return NT_NOT_SUPPORTED;
    if (length != 4)return 0xc0000004U;
    if (process != (void *) ~0ULL)return 0xc0000008U;
    if (!nt_obj_writable(buffer, 4) || (returned && !nt_obj_writable(returned, 4)))return 0xc0000005U;
    *(u32 *) buffer = nt_cookie();
    if (returned)*returned = 4;
    return 0;
}

static int nt_basic_snapshot(nt_system_basic *info) {
    if (boot->descriptor_size < 40 || boot->descriptor_size > boot->map_size)return 0;
    u64 count = 0, lowest = ~0ULL, highest = 0;
    for (u64 offset = 0; range(offset, boot->descriptor_size, boot->map_size); offset += boot->descriptor_size) {
        const u8 *p = (const u8 *) boot->memory_map + offset;
        u32 type = rd32(p);
        /* RAM described by UEFI: loader, boot/runtime services, conventional, ACPI. */
        if (!((type >= 1 && type <= 7) || type == 9 || type == 10))continue;
        u64 page = rd64(p + 8) / 4096, pages = rd64(p + 24);
        if (!pages)continue;
        if (page > 0xffffffffULL || pages > 0x100000000ULL - page || pages > 0xffffffffULL - count)return 0;
        count += pages;
        if (page < lowest)lowest = page;
        if (page + pages - 1 > highest)highest = page + pages - 1;
    }
    if (!count)return 0;
    pg_zero(info, sizeof(*info));
    info->timer = 100000; /* Nominal 10 ms PIT interval, expressed in 100 ns units. */
    info->page_size = 4096;
    info->physical_pages = (u32) count;
    info->lowest_page = (u32) lowest;
    info->highest_page = (u32) highest;
    info->granularity = 65536;
    info->minimum = 65536;
    info->maximum = VM_WINDOW_END - 1;
    info->affinity = 1;
    info->processors = 1;
    return 1;
}

static u32 nt_QuerySystemInformation(u32 info_class, void *buffer, u32 length, u32 *returned) {
    if (info_class != 0)return NT_NOT_SUPPORTED;
    if (returned && !nt_obj_writable(returned, 4))return 0xc0000005U;
    if (returned)*returned = 64;
    if (length != 64)return 0xc0000004U;
    if (!nt_obj_writable(buffer, 64))return 0xc0000005U;
    nt_system_basic info;
    if (!nt_basic_snapshot(&info))return NT_NOT_SUPPORTED;
    *(nt_system_basic *) buffer = info;
    return 0;
}

static u64 nt_info_export(const char *name) {
    if (same(name, "NtQueryInformationProcess"))return (u64) nt_QueryInformationProcess;
    if (same(name, "NtQuerySystemInformation"))return (u64) nt_QuerySystemInformation;
    return 0;
}
