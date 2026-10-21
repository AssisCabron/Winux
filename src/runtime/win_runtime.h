/* Original single-thread Win32 runtime. No Windows implementation bytes. */
#include "../nt/objects/nt_objects.h"
#include "../nt/io/nt_file_core.h"
#include "../process/process_parameters.h"
static obj_table nt_objects;
static io_table nt_io;
#define APP_HEAP_BYTES (256*1024)
static u8 app_heap[APP_HEAP_BYTES] __attribute__((aligned(16)));
static u8 app_teb[0x1800] __attribute__((aligned(16)));
static u8 app_peb[0x800] __attribute__((aligned(16)));
static u8 app_parameters[8192] __attribute__((aligned(16)));
static u16 app_environment[16 * (64 + 256) + 8] __attribute__((aligned(16)));
static u64 app_environment_bytes;
static u16 app_cmdline[256];
static u64 tls_used;
static u32 process_cookie;
static u32 input_mode, output_mode;
static u32 utf8_value, utf8_min, utf8_pending;
#define last_error (*(u32*)(app_teb+0x68))
#define tls_values ((void**)(app_teb+0x1480))

typedef struct {
    u32 extent, requested, used, reserved;
} heap_block;

typedef struct {
    u16 name[64], value[256];
    int used;
} env_entry;

static env_entry app_env[16];

static int inside(const void *p, u64 n, const void *base, u64 size) {
    u64 a = (u64) p, b = (u64) base;
    return a >= b && range(a - b, n, size);
}

static int heap_range(const void *p, u64 n) {
    for (u32 at = 0; at < APP_HEAP_BYTES;) {
        heap_block *h = (heap_block *) (app_heap + at);
        if (h->extent < 16 || h->extent > APP_HEAP_BYTES - at)return 0;
        if (h->used && inside(p, n, h + 1, h->requested))return 1;
        at += h->extent;
    }
    return 0;
}

static int static_tls_range(const void *p, u64 n);

static int win_buffer(const void *p, u64 n) {
    return static_tls_range(p, n) || app_range(p, n) || stack_range(p, n) || heap_range(p, n);
}

static int win_readable(const void *p, u64 n) {
    return win_buffer(p, n) || inside(p, n, app_cmdline, sizeof(app_cmdline)) || inside(p, n, app_teb, sizeof(app_teb))
           || inside(p, n, app_peb, sizeof(app_peb)) || inside(p, n, app_parameters, sizeof(app_parameters)) || inside(
               p, n, app_environment, app_environment_bytes);
}

static u64 read_msr(u32 id) {
    u32 lo, hi;
    __asm__ volatile("rdmsr":"=a"(lo),"=d"(hi):"c"(id));
    return lo | ((u64) hi << 32);
}

static void write_msr(u32 id, u64 value) {
    __asm__ volatile("wrmsr"::"c"(id),"a"((u32) value),"d"((u32)(value >> 32)):"memory");
}

static void zero_bytes(void *p, u64 n) { for (u64 i = 0; i < n; i++)((u8 *) p)[i] = 0; }

static void ascii_wide(u16 *out, const char *s, u32 cap) {
    u32 i = 0;
    while (i + 1 < cap && s[i]) {
        out[i] = (u8) s[i];
        i++;
    }
    out[i] = 0;
}

#include "../process/process_context.h"

static int runtime_reset(const char *name) {
    if (!nt_native_dispose())return 0;
    obj_reset(&nt_objects);
    process_cookie = 0;
    zero_bytes(app_teb, sizeof(app_teb));
    zero_bytes(app_peb, sizeof(app_peb));
    zero_bytes(app_env, sizeof(app_env));
    zero_bytes(app_parameters, sizeof(app_parameters));
    zero_bytes(app_environment, sizeof(app_environment));
    app_environment_bytes = 0;
    zero_bytes(app_heap, sizeof(app_heap));
    *(heap_block *) app_heap = (heap_block){APP_HEAP_BYTES, 0, 0, 0};
    *(u64 *) (app_teb + 0x00) = ~0ULL; /* NT_TIB exception-list sentinel. */
    *(u64 *) (app_teb + 0x08) = (u64) app_stack + sizeof(app_stack);
    *(u64 *) (app_teb + 0x10) = (u64) app_stack;
    *(u64 *) (app_teb + 0x30) = (u64) app_teb;
    *(u64 *) (app_teb + 0x40) = 1;
    *(u64 *) (app_teb + 0x48) = 1;
    *(u64 *) (app_teb + 0x60) = (u64) app_peb;
    *(u64 *) (app_peb + 0x10) = (u64) app_image;
    ascii_wide(app_cmdline, name, 256);
    tls_used = 0;
    input_mode = 7;
    output_mode = 3;
    utf8_value = utf8_min = utf8_pending = 0;
    const char *names[] = {"COMSPEC", "SystemRoot", "PATH"};
    const char *values[] = {"C:\\Windows\\System32\\cmd.exe", "C:\\Windows", "C:\\Windows\\System32"};
    for (unsigned i = 0; i < 3; i++) {
        ascii_wide(app_env[i].name, names[i], 64);
        ascii_wide(app_env[i].value, values[i], 256);
        app_env[i].used = 1;
    }
    return process_context_build();
}

static void *win_GetStdHandle(u32 kind) {
    if (kind == (u32) - 11)return (void *) 1;
    if (kind == (u32) - 12)return (void *) 2;
    if (kind == (u32) - 10)return (void *) 3;
    last_error = 87;
    return (void *) ~0ULL;
}

static u32 win_GetLastError(void) { return last_error; }

static void win_SetLastError(u32 error) {
    last_error = error;
}

static u32 win_GetCurrentProcessId(void) { return (u32) * (u64 *) (app_teb + 0x40); }
static u32 win_GetCurrentThreadId(void) { return (u32) * (u64 *) (app_teb + 0x48); }
static u32 win_GetTickCount(void) { return (u32)(timer_ticks * 10); }
static u64 win_GetTickCount64(void) { return timer_ticks * 10; }

static int win_QueryPerformanceCounter(u64 *value) {
    if (!win_buffer(value, 8)) {
        last_error = 87;
        return 0;
    }
    *value = timer_ticks;
    return 1;
}

static int win_QueryPerformanceFrequency(u64 *value) {
    if (!win_buffer(value, 8)) {
        last_error = 87;
        return 0;
    }
    *value = 100;
    return 1;
}

static u16 *win_GetCommandLineW(void) { return (u16 *) rd64(app_parameters + 0x78); }
static void *win_GetProcessHeap(void) { return (void *) 0x100; }
/* MS x64 nonvolatile integer and vector state is restored even on ExitProcess.
 * One active app only. Exceptions still halt the machine; no user isolation. */
u8 app_fx_state[512] __attribute__((aligned(16)));

__attribute__((used,naked)) static void app_finish(void) {
    __asm__ volatile(
        "fxrstor64 app_fx_state(%rip); movq app_return_sp(%rip),%rsp; popq %r15; popq %r14; popq %r13; popq %r12; popq %rsi; popq %rdi; popq %rbx; popq %rbp; ret"
    );
}

__attribute__((naked,noinline)) static int app_enter(void *entry) {
    __asm__ volatile(
        "pushq %rbp; pushq %rbx; pushq %rdi; pushq %rsi; pushq %r12; pushq %r13; pushq %r14; pushq %r15; fxsave64 app_fx_state(%rip); movq %rsp,app_return_sp(%rip); leaq app_stack+65536(%rip),%rsp; andq $-16,%rsp; subq $32,%rsp; callq *%rcx; jmp app_finish"
    );
}

__attribute__((naked,noreturn)) static void win_ExitProcess(u32 code) {
    __asm__ volatile(
        "subq $40,%rsp; movl %ecx,32(%rsp); callq process_detach; movl 32(%rsp),%eax; addq $40,%rsp; jmp app_finish");
}

static heap_block *heap_find(void *p) {
    for (u32 at = 0; at < APP_HEAP_BYTES;) {
        heap_block *h = (heap_block *) (app_heap + at);
        if (h->extent < 16 || h->extent > APP_HEAP_BYTES - at)return 0;
        if ((void *) (h + 1) == p && h->used)return h;
        at += h->extent;
    }
    return 0;
}

static void *win_HeapAlloc(void *heap, u32 flags, u64 size) {
    if (heap != (void *) 0x100 || (flags & ~9U) || size > APP_HEAP_BYTES - 32)return 0;
    u32 need = (u32)(((size ? size : 1) + 15) & ~15ULL) + 16;
    for (u32 at = 0; at < APP_HEAP_BYTES;) {
        heap_block *h = (heap_block *) (app_heap + at);
        if (h->extent < 16 || h->extent > APP_HEAP_BYTES - at)return 0;
        if (!h->used && h->extent >= need) {
            u32 old = h->extent;
            if (old - need >= 32) {
                *(heap_block *) (app_heap + at + need) = (heap_block){old - need, 0, 0, 0};
                h->extent = need;
            }
            h->requested = (u32) size;
            h->used = 1;
            if (flags & 8)zero_bytes(h + 1, size);
            return h + 1;
        }
        at += h->extent;
    }
    return 0;
}

static int win_HeapFree(void *heap, u32 flags, void *p) {
    if (heap != (void *) 0x100 || (flags & ~1U))return 0;
    if (!p)return 1;
    heap_block *h = heap_find(p);
    if (!h)return 0;
    h->used = 0;
    h->requested = 0;
    for (u32 at = 0; at < APP_HEAP_BYTES;) {
        heap_block *a = (heap_block *) (app_heap + at);
        u32 next = at + a->extent;
        if (next < APP_HEAP_BYTES) {
            heap_block *b = (heap_block *) (app_heap + next);
            if (!a->used && !b->used) {
                a->extent += b->extent;
                continue;
            }
        }
        at += a->extent;
    }
    return 1;
}

static u64 win_HeapSize(void *heap, u32 flags, void *p) {
    heap_block *h = heap_find(p);
    return heap == (void *) 0x100 && !(flags & ~1U) && h ? h->requested : ~0ULL;
}

static void *win_HeapReAlloc(void *heap, u32 flags, void *p, u64 size) {
    heap_block *h = heap_find(p);
    if (heap != (void *) 0x100 || !h || (flags & ~0x19U) || size > APP_HEAP_BYTES - 32)return 0;
    u32 old = h->requested;
    if (size <= h->extent - 16) {
        if ((flags & 8) && size > old)zero_bytes((u8 *) p + old, size - old);
        h->requested = (u32) size;
        return p;
    }
    if (flags & 16)return 0;
    void *out = win_HeapAlloc(heap, flags & 9, size);
    if (!out)return 0;
    for (u32 i = 0; i < old; i++)((u8 *) out)[i] = ((u8 *) p)[i];
    win_HeapFree(heap, 0, p);
    return out;
}

static u32 win_TlsAlloc(void) {
    for (u32 i = 0; i < 64; i++)if (!(tls_used & (1ULL << i))) {
        tls_used |= 1ULL << i;
        tls_values[i] = 0;
        return i;
    }
    last_error = 8;
    return ~0U;
}

static int win_TlsFree(u32 i) {
    if (i >= 64 || !(tls_used & (1ULL << i))) {
        last_error = 87;
        return 0;
    }
    tls_used &= ~(1ULL << i);
    tls_values[i] = 0;
    return 1;
}

static int win_TlsSetValue(u32 i, void *p) {
    if (i >= 64 || !(tls_used & (1ULL << i))) {
        last_error = 87;
        return 0;
    }
    tls_values[i] = p;
    return 1;
}

static void *win_TlsGetValue(u32 i) {
    if (i >= 64 || !(tls_used & (1ULL << i))) {
        last_error = 87;
        return 0;
    }
    last_error = 0;
    return tls_values[i];
}

static int wide_length(const u16 *p, u32 max, u32 *length) {
    for (u32 i = 0; i < max; i++) {
        if (!win_readable(p + i, 2))return 0;
        if (!p[i]) {
            *length = i;
            return 1;
        }
    }
    return 0;
}

static u16 wide_lower(u16 c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

static int env_find(const u16 *name, u32 n) {
    for (unsigned i = 0; i < 16; i++)if (app_env[i].used) {
        u32 j = 0;
        while (j < n && wide_lower(name[j]) == wide_lower(app_env[i].name[j]))j++;
        if (j == n && !app_env[i].name[j])return (int) i;
    }
    return -1;
}

static int win_SetEnvironmentVariableW(const u16 *name, const u16 *value) {
    u32 n, len = 0;
    if (!wide_length(name, 64, &n) || !n || (value && !wide_length(value, 256, &len))) {
        last_error = 87;
        return 0;
    }
    for (u32 i = 0; i < n; i++)if (name[i] == '=') {
        last_error = 87;
        return 0;
    }
    int slot = env_find(name, n);
    if (!value) {
        if (slot < 0) {
            last_error = 203;
            return 0;
        }
        app_env[slot].used = 0;
        process_environment_sync();
        return 1;
    }
    if (slot < 0)for (int i = 0; i < 16; i++)if (!app_env[i].used) {
        slot = i;
        break;
    }
    if (slot < 0) {
        last_error = 8;
        return 0;
    }
    for (u32 i = 0; i <= n; i++)app_env[slot].name[i] = name[i];
    for (u32 i = 0; i <= len; i++)app_env[slot].value[i] = value[i];
    app_env[slot].used = 1;
    process_environment_sync();
    return 1;
}

static u32 win_GetEnvironmentVariableW(const u16 *name, u16 *buffer, u32 size) {
    u32 n;
    if (!wide_length(name, 64, &n) || !n) {
        last_error = 87;
        return 0;
    }
    int i = env_find(name, n);
    if (i < 0) {
        last_error = 203;
        return 0;
    }
    u32 len = 0;
    while (app_env[i].value[len])len++;
    if (size <= len)return len + 1;
    if (!win_buffer(buffer, ((u64) len + 1) * 2)) {
        last_error = 87;
        return 0;
    }
    for (u32 j = 0; j <= len; j++)buffer[j] = app_env[i].value[j];
    return len;
}

/* Explicit CP_UTF8 console byte stream; no implicit Latin-1 conversion. */
static void console_utf8_byte(u8 b) {
    if (utf8_pending) {
        if ((b & 0xc0) == 0x80) {
            utf8_value = (utf8_value << 6) | (b & 63);
            if (!--utf8_pending) {
                u32 c = utf8_value;
                if (c < utf8_min || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff))c = 0xfffd;
                console_unicode(c);
            }
            return;
        }
        utf8_pending = 0;
        console_unicode(0xfffd);
    }
    if (b < 0x80)console_unicode(b);
    else if (b >= 0xc2 && b <= 0xdf) {
        utf8_value = b & 31;
        utf8_pending = 1;
        utf8_min = 0x80;
    } else if (b >= 0xe0 && b <= 0xef) {
        utf8_value = b & 15;
        utf8_pending = 2;
        utf8_min = 0x800;
    } else if (b >= 0xf0 && b <= 0xf4) {
        utf8_value = b & 7;
        utf8_pending = 3;
        utf8_min = 0x10000;
    } else console_unicode(0xfffd);
}

static u32 win_GetConsoleOutputCP(void) { return 65001; }

static int win_SetConsoleOutputCP(u32 codepage) {
    if (codepage != 65001) {
        last_error = 50;
        return 0;
    }
    return 1;
}

static int win_WriteFile(void *handle, const void *data, u32 count, u32 *written, void *overlapped) {
    if (!written || !win_buffer(written, 4)) {
        last_error = 87;
        return 0;
    }
    *written = 0;
    if (handle != (void *) 1 && handle != (void *) 2) {
        last_error = 6;
        return 0;
    }
    if (overlapped || count > 65536 || !win_readable(data, count)) {
        last_error = 87;
        return 0;
    }
    for (u32 i = 0; i < count; i++)console_utf8_byte(((const u8 *) data)[i]);
    *written = count;
    return 1;
}

static int win_WriteConsoleW(void *handle, const u16 *data, u32 count, u32 *written, void *reserved) {
    if (written && !win_buffer(written, 4)) {
        last_error = 87;
        return 0;
    }
    if (written)*written = 0;
    if (handle != (void *) 1 && handle != (void *) 2) {
        last_error = 6;
        return 0;
    }
    if (reserved || count > 32768 || !win_readable(data, (u64) count * 2)) {
        last_error = 87;
        return 0;
    }
    for (u32 i = 0; i < count; i++) {
        u32 c = data[i];
        if (c >= 0xd800 && c <= 0xdbff) {
            if (i + 1 < count && data[i + 1] >= 0xdc00 && data[i + 1] <= 0xdfff)
                c = 0x10000 + ((c - 0xd800) << 10) + (data[++i] - 0xdc00);
            else c = 0xfffd;
        } else if (c >= 0xdc00 && c <= 0xdfff)c = 0xfffd;
        console_unicode(c);
    }
    if (written)*written = count;
    return 1;
}

static int win_GetConsoleMode(void *handle, u32 *mode) {
    if (handle != (void *) 1 && handle != (void *) 2 && handle != (void *) 3) {
        last_error = 6;
        return 0;
    }
    if (!win_buffer(mode, 4)) {
        last_error = 87;
        return 0;
    }
    *mode = handle == (void *) 3 ? input_mode : output_mode;
    return 1;
}

static int win_SetConsoleMode(void *handle, u32 mode) {
    if (handle != (void *) 1 && handle != (void *) 2 && handle != (void *) 3) {
        last_error = 6;
        return 0;
    }
    /* Only the actually implemented default processed/wrap modes are accepted. */
    if ((handle == (void *) 3 && mode != 7) || (handle != (void *) 3 && mode != 3)) {
        last_error = 50;
        return 0;
    }
    if (handle == (void *) 3)input_mode = mode;
    else output_mode = mode;
    return 1;
}

/* API-set aliases are deliberately exact, derived from the CMD import inventory. */
typedef struct {
    const char *name, *contract;
    void *address;
} win_binding;

static int nt_obj_region(const void *p, u64 n, int write) {
    vm_region *r = vm_find((u64) p, n);
    if (!r)return 0;
    unsigned first = (unsigned) (((u64) p - r->base) / 4096), last = (unsigned) (((u64) p - r->base + n - 1) / 4096);
    for (unsigned i = first; i <= last; i++)if (!r->pages[i] || !(r->protection[i] & (write ? 2 : 1)))return 0;
    return 1;
}

static int nt_obj_writable(const void *p, u64 n) { return win_buffer(p, n) || nt_obj_region(p, n, 1); }
static int nt_obj_readable(const void *p, u64 n) { return win_readable(p, n) || nt_obj_region(p, n, 0); }
#include "../nt/io/nt_file_api.h"
#include "../mm/nt_memory.h"
#include "../nt/nt_info.h"
#include "../nt/objects/nt_object_api.h"

static u64 runtime_export(const char *dll, const char *name) {
    if (case_same(dll, "ntdll.dll")) {
        u64 found = nt_memory_export(name);
        if (!found)found = nt_info_export(name);
        return found ? found : nt_object_export(name);
    }
    static const win_binding table[] = {
        {"GetStdHandle", "processenvironment-l1-1-0", win_GetStdHandle},
        {"GetCommandLineW", "processenvironment-l1-1-0", win_GetCommandLineW},
        {"GetModuleHandleW", "libraryloader-l1-1-0", win_GetModuleHandleW},
        {"GetProcAddress", "libraryloader-l1-1-0", win_GetProcAddress},
        {"GetEnvironmentVariableW", "processenvironment-l1-1-0", win_GetEnvironmentVariableW},
        {"SetEnvironmentVariableW", "processenvironment-l1-1-0", win_SetEnvironmentVariableW},
        {"WriteFile", "file-l1-1-0", win_WriteFile},
        {"GetLastError", "errorhandling-l1-1-0", win_GetLastError},
        {"SetLastError", "errorhandling-l1-1-0", win_SetLastError},
        {"GetCurrentProcessId", "processthreads-l1-1-0", win_GetCurrentProcessId},
        {"GetCurrentThreadId", "processthreads-l1-1-0", win_GetCurrentThreadId},
        {"ExitProcess", "processthreads-l1-1-0", win_ExitProcess},
        {"TlsAlloc", "processthreads-l1-1-0", win_TlsAlloc},
        {"TlsFree", "processthreads-l1-1-0", win_TlsFree},
        {"TlsSetValue", "processthreads-l1-1-0", win_TlsSetValue},
        {"TlsGetValue", "processthreads-l1-1-0", win_TlsGetValue},
        {"GetProcessHeap", "heap-l1-1-0", win_GetProcessHeap},
        {"HeapAlloc", "heap-l1-1-0", win_HeapAlloc},
        {"HeapFree", "heap-l1-1-0", win_HeapFree},
        {"HeapSize", "heap-l1-1-0", win_HeapSize},
        {"HeapReAlloc", "heap-l1-1-0", win_HeapReAlloc},
        {"WriteConsoleW", "console-l1-1-0", win_WriteConsoleW},
        {"GetConsoleOutputCP", "console-l1-1-0", win_GetConsoleOutputCP},
        {"SetConsoleOutputCP", "console-l1-1-0", win_SetConsoleOutputCP},
        {"GetConsoleMode", "console-l1-1-0", win_GetConsoleMode},
        {"SetConsoleMode", "console-l1-1-0", win_SetConsoleMode},
        {"GetTickCount", "sysinfo-l1-1-0", win_GetTickCount},
        {"GetTickCount64", "sysinfo-l1-1-0", win_GetTickCount64},
        {"QueryPerformanceCounter", "profile-l1-1-0", win_QueryPerformanceCounter},
        {"QueryPerformanceFrequency", "profile-l1-1-0", win_QueryPerformanceFrequency}
    };
    for (unsigned i = 0; i < sizeof(table) / sizeof(table[0]); i++)
        if (same(name, table[i].name)) {
            if (case_same(dll, "kernel32.dll"))return (u64) table[i].address;
            char contract[100] = "api-ms-win-core-";
            u32 n = 16; /* prefix is 16 bytes */
            const char *s = table[i].contract;
            while (*s)contract[n++] = *s++;
            contract[n++] = '.';
            contract[n++] = 'd';
            contract[n++] = 'l';
            contract[n++] = 'l';
            contract[n] = 0;
            if (case_same(dll, contract))return (u64) table[i].address;
        }
    return 0;
}
