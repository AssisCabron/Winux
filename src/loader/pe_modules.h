/* Original bounded PE module graph, export resolver and dependency initialization. */
static u64 module_used;
static unsigned init_order[16], init_count;
static int graph_error, graph_running, native_graph;
#include "../../build/apisets.h"

static pe_module *module_load(const ram_file *file, unsigned depth);

static const char *module_string(pe_module *m, u64 rva) {
    for (u64 n = 0; n < 256 && range(rva, n + 1, m->size); n++)if (!m->base[rva + n])return
            (const char *) m->base + rva;
    return 0;
}

static const ram_file *module_file(const char *name) {
    for (unsigned i = 0; i < RAM_FILE_COUNT; i++)if (case_same(name, ram_files[i].name))return &ram_files[i];
    return 0;
}

static pe_module *module_named(const char *name) {
    for (unsigned i = 0; i < module_count; i++)if (case_same(name, modules[i].file->name))return &modules[i];
    return 0;
}

/* Resolve schema contracts using the importing/forwarding module as parent. */
static const char *api_host(const char *dll, const char *parent) {
    char key[128];
    u32 n = 0;
    while (dll[n] && n < 127) {
        key[n] = dll[n];
        n++;
    }
    if (dll[n])return 0;
    key[n] = 0;
    if (n >= 4 && case_same(key + n - 4, ".dll"))key[n -= 4] = 0;
    if (n < 4)return dll;
    if (!((key[0] == 'a' || key[0] == 'A') && (key[1] == 'p' || key[1] == 'P') && (key[2] == 'i' || key[2] == 'I') &&
          key[3] == '-') &&
        !((key[0] == 'e' || key[0] == 'E') && (key[1] == 'x' || key[1] == 'X') && (key[2] == 't' || key[2] == 'T') &&
          key[3] == '-'))
        return dll;
    const char *contract = 0;
    for (u32 i = 0; i < API_SET_COUNT; i++)if (case_same(key, api_sets[i].contract)) {
        contract = api_sets[i].contract;
        break;
    }
    if (!contract) {
        u32 end = n;
        while (end && key[end - 1] != '-')end--;
        if (!end)return 0;
        key[end] = 0;
        for (u32 i = 0; i < API_SET_COUNT; i++) {
            const char *c = api_sets[i].contract;
            u32 j = 0;
            while (j < end && c[j] && ((c[j] | 32) == (key[j] | 32)))j++;
            if (j != end)continue;
            /* Require the remainder to be only the final revision component. */
            int valid = 1;
            for (u32 k = end; c[k]; k++)if (c[k] < '0' || c[k] > '9')valid = 0;
            if (valid) {
                if (contract && !case_same(contract, c))return 0;
                contract = c;
            }
        }
    }
    if (!contract)return 0;
    const char *fallback = 0;
    for (u32 i = 0; i < API_SET_COUNT; i++)
        if (case_same(contract, api_sets[i].contract)) {
            const api_set_entry *e = &api_sets[i];
            if (e->parent[0] && case_same(e->parent, parent))return e->host[0] ? e->host : 0;
            if (!e->parent[0])fallback = e->host;
        }
    return fallback && fallback[0] ? fallback : 0;
}

static u64 module_export(pe_module *m, const char *name, u32 ordinal, unsigned depth);

static u64 resolve_import(const char *dll, const char *name, u32 ordinal, unsigned depth, const char *parent) {
    if (depth > 64)return 0;
    u64 builtin = !native_graph && name ? runtime_export(dll, name) : 0;
    if (builtin)return builtin;
    dll = api_host(dll, parent);
    if (!dll)return 0;
    pe_module *m = module_named(dll);
    if (!m) {
        if (graph_running)return 0;
        const ram_file *f = module_file(dll);
        if (!f)return 0;
        m = module_load(f, depth + 1);
    }
    if (!m || m->state == 3 || !m->dll)return 0;
    return module_export(m, name, ordinal, depth + 1);
}

static u64 module_export(pe_module *m, const char *name, u32 ordinal, unsigned depth) {
    if (depth > 64 || !m->dirs)return 0;
    u32 rva = rd32(m->base + m->optional + 112), size = rd32(m->base + m->optional + 116);
    if (size < 40 || !range(rva, size, m->size))return 0;
    const u8 *e = m->base + rva;
    u32 first = rd32(e + 16), count = rd32(e + 20), names = rd32(e + 24), functions = rd32(e + 28), strings =
            rd32(e + 32), ordinals = rd32(e + 36);
    if (count > 65536 || names > 65536 || !range(functions, (u64) count * 4, m->size) || !
        range(strings, (u64) names * 4, m->size) || !range(ordinals, (u64) names * 2, m->size))return 0;
    u32 index = ~0U;
    if (name) {
        for (u32 i = 0; i < names; i++) {
            const char *candidate = module_string(m, rd32(m->base + strings + i * 4));
            if (!candidate)return 0;
            if (same(name, candidate)) {
                index = rd16(m->base + ordinals + i * 2);
                break;
            }
        }
    } else if (ordinal >= first)index = ordinal - first;
    if (index >= count)return 0;
    u32 target = rd32(m->base + functions + index * 4);
    if (!target || !range(target, 1, m->size))return 0;
    if (target >= rva && (u64) target - rva < size) {
        const char *forward = module_string(m, target);
        if (!forward)return 0;
        u32 split = 0, len = 0;
        while (forward[len]) {
            if (forward[len] == '.')split = len;
            len++;
        }
        if ((u64) target + len + 1 > (u64) rva + size || !split || split >= 95 || split + 1 >= len)return 0;
        char dll[100];
        for (u32 i = 0; i < split; i++)dll[i] = forward[i];
        dll[split] = 0;
        if (split < 4 || !case_same(dll + split - 4, ".dll")) {
            dll[split++] = '.';
            dll[split++] = 'd';
            dll[split++] = 'l';
            dll[split++] = 'l';
            dll[split] = 0;
        }
        const char *symbol = forward + len; /* reset from original separator, not appended suffix */
        u32 original_split = 0;
        for (u32 i = 0; i < len; i++)if (forward[i] == '.')original_split = i;
        symbol = forward + original_split + 1;
        if (*symbol == '#') {
            u32 n = 0;
            symbol++;
            if (!*symbol)return 0;
            while (*symbol) {
                if (*symbol < '0' || *symbol > '9' || n > 6553)return 0;
                n = n * 10 + (u32)(*symbol++ - '0');
                if (n > 65535)return 0;
            }
            return resolve_import(dll, 0, n, depth + 1, m->file->name);
        }
        return resolve_import(dll, symbol, 0, depth + 1, m->file->name);
    }
    return (u64) m->base + target; /* Data and function exports both return their actual VA. */
}

static int module_relocate(pe_module *m, u64 preferred, u16 characteristics, u16 dll_flags) {
    u64 delta = (u64) m->base - preferred;
    if (!delta)return 1;
    u32 rva = m->dirs > 5 ? rd32(m->base + m->optional + 152) : 0, size = m->dirs > 5
                                                                              ? rd32(m->base + m->optional + 156)
                                                                              : 0;
    if (!rva && !size && !(characteristics & 1) && (dll_flags & 0x40))return 1;
    if (!rva || !size || !range(rva, size, m->size))return 0;
    for (u64 off = 0; off < size;) {
        if (!range(off, 8, size))return 0;
        u8 *b = m->base + rva + off;
        u32 page = rd32(b), bytes = rd32(b + 4);
        if (bytes < 8 || (bytes & 1) || !range(off, bytes, size))return 0;
        for (u32 i = 8; i < bytes; i += 2) {
            u16 item = rd16(b + i);
            u32 type = item >> 12;
            if (!type)continue;
            u64 at = (u64) page + (item & 4095);
            if (type != 10 || !range(at, 8, m->size))return 0;
            *(u64 *) (m->base + at) += delta;
        }
        off += bytes;
    }
    return 1;
}

static int module_bind(pe_module *m, unsigned depth) {
    u32 rva = m->dirs > 1 ? rd32(m->base + m->optional + 120) : 0, size = m->dirs > 1
                                                                              ? rd32(m->base + m->optional + 124)
                                                                              : 0;
    if (!rva && !size)return 1;
    if (!range(rva, size, m->size) || size < 20)return 0;
    unsigned imports = 0, missing = 0;
    int end = 0;
    for (u64 off = 0; range(off, 20, size); off += 20) {
        u8 *d = m->base + rva + off;
        if (!rd32(d) && !rd32(d + 4) && !rd32(d + 8) && !rd32(d + 12) && !rd32(d + 16)) {
            end = 1;
            break;
        }
        const char *dll = module_string(m, rd32(d + 12));
        if (!dll)return 0;
        u64 lookup = rd32(d), iat = rd32(d + 16);
        if (!lookup)lookup = iat;
        int terminated = 0;
        for (u32 i = 0; i < 4096; i++) {
            u64 source = lookup + (u64) i * 8, target = iat + (u64) i * 8;
            if (!range(source, 8, m->size) || !range(target, 8, m->size))return 0;
            u64 value = rd64(m->base + source);
            if (!value) {
                terminated = 1;
                break;
            }
            int by_ordinal = (value >> 63) != 0;
            const char *name = by_ordinal ? 0 : module_string(m, value + 2);
            if (!by_ordinal && !name)return 0;
            if (by_ordinal && (value & 0x7fffffffffff0000ULL))return 0;
            u64 address = resolve_import(dll, name, (u16) value, depth + 1, m->file->name);
            imports++;
            if (!address) {
                if (missing < 10) {
                    print("  missing ");
                    print(dll);
                    print("!");
                    if (name)print(name);
                    else {
                        print("#");
                        number((u16) value);
                    }
                    print("\n");
                }
                missing++;
            } else *(u64 *) (m->base + target) = address;
        }
        if (!terminated)return 0;
    }
    if (!end)return 0;
    print("Module ");
    print(m->file->name);
    print(" - Imports: ");
    number(imports);
    print("; unresolved: ");
    number(missing);
    print("\n");
    return missing ? -1 : 1;
}

static pe_module *module_load(const ram_file *f, unsigned depth) {
    pe_module *existing = module_named(f->name);
    if (existing)return existing;
    u32 entry, size;
    u16 sections;
    if (depth > 64 || module_count >= 16 || !pe_valid(f->data, f->size, &entry, &size, &sections)) {
        graph_error = 1;
        return 0;
    }
    u64 pe = rd32(f->data + 60), o = pe + 24, table = o + rd16(f->data + pe + 20), offset =
            (module_used + 65535) & ~65535ULL;
    if (!boot->app_arena || !range(offset, size, boot->app_arena_size)) {
        graph_error = 1;
        return 0;
    }
    pe_module *m = &modules[module_count++];
    *m = (pe_module)
    {
        f, (u8 *) boot->app_arena + offset, size, entry, (u32) o, rd32(f->data + o + 108), 1, (
            rd16(f->data + pe + 22) & 0x2000) != 0, 0
    };
    module_used = offset + size;
    u32 headers = rd32(f->data + o + 60);
    if (m->dirs > 16) {
        graph_error = 1;
        m->state = 3;
        return m;
    }
    zero_bytes(m->base, size);
    for (u32 i = 0; i < headers; i++)m->base[i] = f->data[i];
    for (u32 i = 0; i < sections; i++) {
        const u8 *s = f->data + table + i * 40;
        u32 va = rd32(s + 12), raw = rd32(s + 16), pos = rd32(s + 20), vs = rd32(s + 8), extent = raw > vs ? raw : vs;
        if (va < headers) {
            graph_error = 1;
            m->state = 3;
            return m;
        }
        for (u32 j = 0; j < i; j++) {
            const u8 *t = f->data + table + j * 40;
            u32 start = rd32(t + 12), a = rd32(t + 8), b = rd32(t + 16);
            if (extent && va < (u64) start + (a > b ? a : b) && start < (u64) va + extent) {
                graph_error = 1;
                m->state = 3;
                return m;
            }
        }
        for (u32 j = 0; j < raw; j++)m->base[va + j] = f->data[pos + j];
    }
    if (!module_relocate(m, rd64(f->data + o + 24), rd16(f->data + pe + 22), rd16(f->data + o + 70))) {
        graph_error = 1;
        m->state = 3;
        return m;
    }
    int bound = module_bind(m, depth);
    if (bound <= 0) {
        if (!bound)graph_error = 1;
        m->state = 3;
        return m;
    }
    m->state = 2;
    init_order[init_count++] = (unsigned) (m - modules);
    return m;
}

static void *win_GetModuleHandleW(const u16 *name) {
    if (!name)return app_image;
    u32 n;
    if (!wide_length(name, 96, &n)) {
        last_error = 87;
        return 0;
    }
    char ascii[100];
    for (u32 i = 0; i <= n; i++) {
        if (name[i] > 127) {
            last_error = 126;
            return 0;
        }
        ascii[i] = (char) name[i];
    }
    if (case_same(ascii, "kernel32.dll"))return (void *) 0x200;
    pe_module *m = module_named(ascii);
    if (m && m->state == 2)return m->base;
    last_error = 126;
    return 0;
}

static void *win_GetProcAddress(void *module, const char *name) {
    int ordinal = (u64) name <= 65535;
    if (!ordinal) {
        for (u32 i = 0; i < 256; i++) {
            if (!win_readable(name + i, 1)) {
                last_error = 87;
                return 0;
            }
            if (!name[i])break;
            if (i == 255) {
                last_error = 87;
                return 0;
            }
        }
    }
    u64 result = 0;
    if (module == (void *) 0x200 && !ordinal)result = runtime_export("kernel32.dll", name);
    else for (unsigned i = 0; i < module_count; i++)if (module == modules[i].base && modules[i].state == 2) {
        result = module_export(&modules[i], ordinal ? 0 : name, (u16)(u64)name, 0);
        break;
    }
    if (!result)last_error = 127;
    return (void *) result;
}

static void detach_module_graph(void *reserved) {
    for (unsigned i = init_count; i; i--) {
        u32 idx = init_order[i - 1];
        pe_module *m = &modules[idx];
        if (tls_started[idx]) {
            tls_started[idx] = 0;
            notify_tls(idx, 0);
        }
        if (m->attached) {
            m->attached = 0;
            ((int(*)(void *, u32, void *)) (m->base + m->entry))(m->base, 0, reserved);
            serial("DLL_DETACH: ");
            serial(m->file->name);
            serial("\r\n");
        }
    }
}

__attribute__((used,noinline)) static void process_detach(void) { detach_module_graph((void *) 1); }

static int start_module_graph(void) {
    graph_running = 1;
    for (unsigned i = 0; i < init_count; i++) {
        u32 idx = init_order[i];
        pe_module *m = &modules[idx];
        tls_started[idx] = 1;
        notify_tls(idx, 1);
        if (!m->dll || !m->entry)continue;
        if (!((int(*)(void *, u32, void *)) (m->base + m->entry))(m->base, 1, (void *) 1)) {
            serial("DLL_ATTACH_FAILED\r\n");
            m->attached = 1;
            detach_module_graph(0);
            return 1114;
        }
        m->attached = 1;
        serial("DLL_ATTACH: ");
        serial(m->file->name);
        serial("\r\n");
    }
    int code = ((int(*)(void)) (modules[0].base + modules[0].entry))();
    process_detach();
    return code;
}
