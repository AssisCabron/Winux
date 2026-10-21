#include "../../build/ramfs.h"
#include "../mm/memory_limits.h"

static int same(const char *a, const char *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static const ram_file *find_file(const char *name) {
    for (unsigned i = 0; i < RAM_FILE_COUNT; i++)if (same(name, ram_files[i].name))return &ram_files[i];
    return 0;
}

static u8 page_pool[PAGE_POOL_COUNT][4096] __attribute__((aligned(4096)));
static u8 page_used[PAGE_POOL_COUNT];

static void *page_alloc(void) {
    for (unsigned i = 0; i < PAGE_POOL_COUNT; i++)
        if (!page_used[i]) {
            page_used[i] = 1;
            for (unsigned j = 0; j < 4096; j++)page_pool[i][j] = 0;
            return page_pool[i];
        }
    return 0;
}

static int page_free(void *p) {
    u64 a = (u64) p, b = (u64) page_pool;
    if (a < b || a - b >= sizeof(page_pool) || (a - b) % 4096)return 0;
    unsigned i = (unsigned) ((a - b) / 4096);
    if (!page_used[i])return 0;
    page_used[i] = 0;
    return 1;
}

static int pool_test(void) {
    /* Diagnostic scratch, single-thread shell only; avoid a large kernel stack frame. */
    static void *p[PAGE_POOL_COUNT];
    unsigned count = 0;
    int ok = 1;
    for (; count < PAGE_POOL_COUNT; count++) {
        p[count] = page_alloc();
        if (!p[count]) {
            ok = 0;
            break;
        }
        ((u8 *) p[count])[0] = 0xa5;
    }
    if (count == PAGE_POOL_COUNT && page_alloc())ok = 0;
    for (unsigned i = 0; i < count; i++)if (!page_free(p[i]))ok = 0;
    if (count && page_free(p[0]))ok = 0;
    void *q = page_alloc();
    if (!q || *(u8 *) q)ok = 0;
    if (q)page_free(q);
    return ok;
}

static u16 rd16(const u8 *p) { return p[0] | ((u16) p[1] << 8); }
static u32 rd32(const u8 *p) { return rd16(p) | ((u32) rd16(p + 2) << 16); }
static int range(u64 offset, u64 count, u64 size) { return offset <= size && count <= size - offset; }

static int pe_valid(const u8 *data, u64 size, u32 *entry, u32 *image_size, u16 *sections) {
    if (size < 64 || rd16(data) != 0x5a4d)return 0;
    u32 pe = rd32(data + 60);
    if (!range(pe, 24, size) || rd32(data + pe) != 0x4550 || rd16(data + pe + 4) != 0x8664)return 0;
    u16 n = rd16(data + pe + 6), opt = rd16(data + pe + 20);
    u64 o = (u64) pe + 24;
    if (!n || n > 96 || opt < 240 || !range(o, opt, size) || rd16(data + o) != 0x20b)return 0;
    u32 images = rd32(data + o + 56), headers = rd32(data + o + 60), ep = rd32(data + o + 16);
    u64 table = o + opt;
    if (!images || images > 64 * 1024 * 1024 || headers > images || headers > size || ep >= images || !range(
            table, (u64) n * 40, headers))
        return 0;
    int executable_entry = 0;
    for (u32 i = 0; i < n; i++) {
        const u8 *s = data + table + i * 40;
        u32 vs = rd32(s + 8), va = rd32(s + 12), raw = rd32(s + 16), pos = rd32(s + 20);
        u32 extent = vs > raw ? vs : raw;
        if (!range(va, extent, images) || (raw && !range(pos, raw, size)))return 0;
        if (ep >= va && (u64) ep - va < extent && (rd32(s + 36) & 0x20000000))executable_entry = 1;
    }
    if (!executable_entry && !((rd16(data + pe + 22) & 0x2000) && ep == 0))return 0;
    *entry = ep;
    *image_size = images;
    *sections = n;
    return 1;
}

static void memory_info(void) {
    u64 available = 0, descriptors = 0;
    if (boot->descriptor_size >= 40 && boot->descriptor_size <= boot->map_size)
        for (u64 off = 0; range(off, boot->descriptor_size, boot->map_size); off += boot->descriptor_size) {
            const u8 *p = (const u8 *) boot->memory_map + off;
            descriptors++;
            if (rd32(p) == 7)available += (*(const u64 *) (p + 24)) * 4096;
        }
    print("Boot memory map: ");
    number(descriptors);
    print(" descriptors\nConventional memory at boot: ");
    number(available / 1024 / 1024);
    print(" MiB (not reclaimed)\n");
    unsigned used = 0;
    for (unsigned i = 0; i < PAGE_POOL_COUNT; i++)used += page_used[i];
    print("Reserved kernel pool: ");
    number(PAGE_POOL_COUNT - used);
    print(" / ");
    number(PAGE_POOL_COUNT);
    print(" free pages, 4 KiB each\nOwn page tables active. No process isolation yet.\n");
}

static int pe_test(void) {
    const ram_file *f = find_file("sample.pe");
    u32 e, s;
    u16 n;
    static u8 corrupt[1024];
    if (!f || f->size != sizeof(corrupt) || !pe_valid(f->data, f->size, &e, &s, &n))return 0;
    if (pe_valid(f->data, 63, &e, &s, &n) || pe_valid(f->data, 512, &e, &s, &n))return 0;
    for (unsigned i = 0; i < sizeof(corrupt); i++)corrupt[i] = f->data[i];
    for (unsigned i = 60; i < 64; i++)corrupt[i] = 255;
    if (pe_valid(corrupt, sizeof(corrupt), &e, &s, &n))return 0;
    for (unsigned i = 0; i < sizeof(corrupt); i++)corrupt[i] = f->data[i];
    u32 table = rd32(corrupt + 60) + 24 + 240;
    for (unsigned i = 20; i < 24; i++)corrupt[table + i] = 255;
    if (pe_valid(corrupt, sizeof(corrupt), &e, &s, &n))return 0;
    return 1;
}

#include "../tests/memory/paging_test.h"
#include "../mm/vm_regions.h"
#include "../loader/pe_loader.h"
#include "../nt/nt_syscall.h"
#include "../tests/memory/nt_partial_test.h"
#include "../tests/process/nt_info_test.h"
#include "../tests/runtime/nt_nls_test.h"
#include "../nt/io/nt_console_context.h"
#include "../runtime/windows/kb_nls_context.h"
#include "../runtime/windows/kb_inputs_context.h"
#include "../process/nt_native_context.h"
#include "../tests/process/process_context_test.h"
#include "../tests/runtime/nt_lock_test.h"
#include "../tests/process/nt_process_parameters_test.h"
#include "../tests/process/nt_native_context_test.h"
#include "../tests/runtime/nt_fls_test.h"
#include "../tests/runtime/kb_codepage_test.h"
#include "../tests/loader/nt_image_test.h"
#include "../tests/io/nt_file_test.h"
#include "../tests/console/nt_console_test.h"
#ifndef WINUX_STARTUP_TEST_HEADER
#define WINUX_STARTUP_TEST_HEADER "../tests/runtime/nt_startup_test.h"
#endif
#include WINUX_STARTUP_TEST_HEADER
#include "../tests/memory/nt_heap_bootstrap_test.h"
#include "../tests/objects/nt_object_test.h"

static void command(char *line, unsigned depth) {
    while (*line == ' ')line++;
    if (!*line || *line == '#')return;
    char *arg = line;
    while (*arg && *arg != ' ')arg++;
    if (*arg) {
        *arg++ = 0;
        while (*arg == ' ')arg++;
    }
    if (same(line, "help"))
        print(
            "help  cls  ver  mem  uptime  dir\ntype <file>  run <file>  pe <file>  imports <file>\necho <text>  alloc-test  pe-test  fault-test\nnt-missing  nt-missing-reset\n");
    else if (same(line, "clear") || same(line, "cls"))console_clear();
    else if (same(line, "about") || same(line, "ver"))
        print(
            "Winux 0.38 - original experimental kernel\nWin32 contract tests: GS context, heap, TLS, environment, UTF-16.\nGeneral Windows applications and drivers: not supported.\n");
    else if (same(line, "echo")) {
        print(arg);
        print("\n");
    } else if (same(line, "cmd")) {
        const ram_file *f = find_file("cmd.exe");
        if (f)pe_load(f, 1);
        else print("cmd.exe is not included in this build.\n");
    } else if (same(line, "mem"))memory_info();
    else if (same(line, "uptime")) {
        print("Ticks (100 Hz): ");
        number(timer_ticks);
        print("; idle wakes: ");
        number(idle_wakes);
        print("\n");
    } else if (same(line, "alloc-test"))
        print(pool_test()
                  ? "ALLOC_TEST_OK: exhaustion, free, double-free, zeroing\n"
                  : "ALLOC_TEST_FAILED\n");
    else if (same(line, "pe-test"))
        print(pe_test()
                  ? "PE_TEST_OK: bounds, truncation and overflow rejection\n"
                  : "PE_TEST_FAILED\n");
    else if (same(line, "tls-test"))print(tls_bounds_test() ? "TLS_BOUNDS_OK\n" : "TLS_BOUNDS_FAILED\n");
    else if (same(line, "page-test"))
        print(paging_test()
                  ? "PAGING_TEST_OK: aliases, remap, protection, executable page\n"
                  : "PAGING_TEST_FAILED\n");
    else if (same(line, "page-fault-test"))paging_fault_test(0);
    else if (same(line, "vm-test"))
        print(vm_test()
                  ? "VM_REGIONS_OK: reserve, commit, rollback, protect, decommit, release\n"
                  : "VM_REGIONS_FAILED\n");
    else if (same(line, "vm-query-test"))print(vm_query_test() ? "VM_QUERY_OK\n" : "VM_QUERY_FAILED\n");
    else if (same(line, "nt-missing"))nt_missing_report();
    else if (same(line, "nt-missing-reset")){nt_missing_reset();print("NT_MISSING_RESET_OK\n");}
    else if (same(line, "nt-stub-test"))print(nt_stub_test() ? "NT_STUB_OK\n" : "NT_STUB_FAILED\n");
    else if (same(line, "nt-partial-test"))print(nt_partial_test() ? "NT_PARTIAL_OK\n" : "NT_PARTIAL_FAILED\n");
    else if (same(line, "nt-info-test"))print(nt_info_test() ? "NT_INFO_OK\n" : "NT_INFO_FAILED\n");
    else if (same(line, "nt-nls-test"))print(nt_nls_test() ? "NT_NLS_TEST_OK\n" : "NT_NLS_TEST_FAILED\n");
    else if (same(line, "process-context-test"))
        print(process_context_test()
                  ? "PROCESS_CONTEXT_TEST_OK\n"
                  : "PROCESS_CONTEXT_TEST_FAILED\n");
    else if (same(line, "nt-startup-test"))
        print(
            nt_startup_test() ? "NT_STARTUP_TEST_OK\n" : "NT_STARTUP_TEST_FAILED\n");
    else if (same(line, "nt-console-test"))
        print(
            nt_console_test() ? "NT_CONSOLE_TEST_OK\n" : "NT_CONSOLE_TEST_FAILED\n");
    else if (same(line, "nt-file-test"))print(nt_file_test() ? "NT_FILE_TEST_OK\n" : "NT_FILE_TEST_FAILED\n");
    else if (same(line, "nt-image-test"))print(nt_image_test() ? "NT_IMAGE_TEST_OK\n" : "NT_IMAGE_TEST_FAILED\n");
    else if (same(line, "kb-codepage-test"))print(kb_codepage_test() ? "KB_CP_TEST_OK\n" : "KB_CP_TEST_FAILED\n");
    else if (same(line, "nt-fls-test"))print(nt_fls_test() ? "NT_FLS_TEST_OK\n" : "NT_FLS_TEST_FAILED\n");
    else if (same(line, "nt-native-context-test"))
        print(nt_native_context_test()
                  ? "NT_NATIVE_CONTEXT_TEST_OK\n"
                  : "NT_NATIVE_CONTEXT_TEST_FAILED\n");
    else if (same(line, "nt-heap-bootstrap-test"))
        print(nt_heap_bootstrap_test()
                  ? "NT_HEAP_BOOTSTRAP_OK\n"
                  : "NT_HEAP_BOOTSTRAP_FAILED\n");
    else if (same(line, "nt-lock-test"))print(nt_lock_test() ? "NT_LOCK_TEST_OK\n" : "NT_LOCK_TEST_FAILED\n");
    else if (same(line, "nt-heap-seed-test"))print(nt_heap_seed_test() ? "NT_HEAP_SEED_OK\n" : "NT_HEAP_SEED_FAILED\n");
    else if (same(line, "nt-object-test"))print(nt_object_test() ? "NT_OBJECT_OK\n" : "NT_OBJECT_FAILED\n");
    else if (same(line, "nx-fault-test"))paging_fault_test(1);
    else if (same(line, "fault-test")) {
        print("Deliberate invalid-opcode exception; this VM will halt.\n");
        __asm__ volatile("ud2");
    } else if (same(line, "ls") || same(line, "dir")) {
        for (unsigned i = 0; i < RAM_FILE_COUNT; i++) {
            print(ram_files[i].name);
            print("  ");
            number(ram_files[i].size);
            print(" bytes\n");
        }
    } else if (same(line, "cat") || same(line, "type") || same(line, "run") || same(line, "pe") ||
               same(line, "imports")) {
        const ram_file *f = find_file(arg);
        if (!f) {
            print("ERROR: file not found\n");
            return;
        }
        if (same(line, "pe")) {
            u32 e, s;
            u16 n;
            if (!pe_valid(f->data, f->size, &e, &s, &n)) {
                print("PE_REJECTED: invalid or unsupported image\n");
                return;
            }
            print("PE_OK AMD64 PE32+ sections=");
            number(n);
            print(" image=");
            number(s);
            print(" entry=");
            print_hex(e);
            print("\n");
        } else if (same(line, "imports"))pe_load(f, 0);
        else if (same(line, "cat") || same(line, "type")) {
            for (u64 i = 0; i < f->size; i++) {
                u8 c = f->data[i];
                console_char((c >= 32 && c < 127) || c == '\n' || c == '\r' || c == '\t' ? (char) c : '.');
            }
            print("\n");
        } else {
            u64 len = 0;
            while (arg[len])len++;
            if (len >= 4 && same(arg + len - 4, ".exe")) {
                pe_load(f, 1);
                return;
            }
            if (len < 4 || !same(arg + len - 4, ".wxs")) {
                print("ERROR: run accepts .wxs or .exe files\n");
                return;
            }
            if (depth >= 4) {
                print("ERROR: script nesting limit\n");
                return;
            }
            char buf[128];
            unsigned at = 0, lines = 0;
            for (u64 i = 0; i <= f->size; i++) {
                u8 c = i == f->size ? '\n' : f->data[i];
                if (c == '\r')continue;
                if (c == '\n') {
                    buf[at] = 0;
                    command(buf, depth + 1);
                    at = 0;
                    if (++lines >= 64) {
                        print("SCRIPT_LIMIT: 64 lines\n");
                        break;
                    }
                } else if (c < 32 || c >= 127 || at >= sizeof(buf) - 1) {
                    print("ERROR: invalid or long script line\n");
                    return;
                } else buf[at++] = (char) c;
            }
        }
    } else print("ERROR: unknown command; type help\n");
}

static void shell(void) {
    char line[128];
    unsigned at = 0;
    int previous_cr = 0;
    print("\nType help to begin.\nwinux> ");
    serial("KERNEL: CONSOLE_READY\r\n");
    for (;;) {
        __asm__ volatile("cli");
        int c = 0;
        if (inb(0x3fd) & 1)c = inb(0x3f8);
        else c = keyboard_char();
        if (c) {
            __asm__ volatile("sti");
            if (c == '\n' && previous_cr) {
                previous_cr = 0;
                continue;
            }
            previous_cr = c == '\r';
            if (c == '\r' || c == '\n') {
                console_char('\n');
                line[at] = 0;
                command(line, 0);
                at = 0;
                print("\nwinux> ");
            } else if (c == 8 || c == 127) {
                if (at) {
                    --at;
                    console_char('\b');
                }
            } else if (c >= 32 && c < 127 && at < sizeof(line) - 1) {
                line[at++] = (char) c;
                console_char((char) c);
            }
        } else {
            idle_wakes++;
            __asm__ volatile("sti; hlt":::"memory");
        }
    }
}
