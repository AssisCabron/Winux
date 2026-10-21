/* Static PE TLS for the single application thread. No dynamic TLS module loading. */
static u8 static_tls_data[64 * 1024] __attribute__((aligned(8192)));
static void *static_tls_vector[16];
static u64 static_tls_used;
static int static_tls_range(const void *p, u64 n) { return inside(p, n, static_tls_data, static_tls_used); }
static u64 tls_callbacks[16][64];
static u32 tls_callback_count[16];
static u8 tls_started[16];

static int module_address(pe_module *m, u64 a, u64 n) {
    return a >= (u64) m->base && range(a - (u64) m->base, n, m->size);
}

static int module_code(pe_module *m, u64 a) {
    if (!module_address(m, a, 1))return 0;
    u32 pe = rd32(m->base + 60), table = pe + 24 + rd16(m->base + pe + 20);
    u64 r = a - (u64) m->base;
    for (u32 i = 0; i < rd16(m->base + pe + 6); i++) {
        const u8 *s = m->base + table + i * 40;
        u32 n = rd32(s + 8), raw = rd32(s + 16);
        if (n < raw)n = raw;
        if ((rd32(s + 36) & 0x20000000) && r >= rd32(s + 12) && range(r - rd32(s + 12), 1, n))return 1;
    }
    return 0;
}

static int prepare_static_tls(void) {
    zero_bytes(static_tls_data, sizeof(static_tls_data));
    zero_bytes(static_tls_vector, sizeof(static_tls_vector));
    zero_bytes(tls_callback_count, sizeof(tls_callback_count));
    zero_bytes(tls_started, sizeof(tls_started));
    static_tls_used = 0;
    u32 slots = 0;
    for (u32 i = 0; i < module_count; i++) {
        pe_module *m = &modules[i];
        if (m->dirs <= 9)continue;
        u32 r = rd32(m->base + m->optional + 184), size = rd32(m->base + m->optional + 188);
        if (!r && !size)continue;
        if (!r || size < 40 || !range(r, size, m->size))return 0;
        const u8 *t = m->base + r;
        u64 start = rd64(t), end = rd64(t + 8), index = rd64(t + 16), callbacks = rd64(t + 24);
        u32 zero = rd32(t + 32), flags = rd32(t + 36), align_code = (flags >> 20) & 15;
        if (end < start || !module_address(m, index, 4) || (flags & ~0xf00000U) || align_code == 15)return 0;
        u64 bytes = end - start;
        if (start && !module_address(m, start, bytes))return 0;
        if ((!start) != (!end))return 0;
        u64 alignment = align_code ? 1ULL << (align_code - 1) : 16;
        if (alignment < 16)alignment = 16;
        u64 at = (static_tls_used + alignment - 1) & ~(alignment - 1), total = bytes + (u64) zero;
        if (slots >= 16 || !range(at, total ? total : 1, sizeof(static_tls_data)))return 0;
        for (u64 j = 0; j < bytes; j++)static_tls_data[at + j] = ((u8 *) start)[j];
        *(u32 *) index = slots;
        static_tls_vector[slots++] = static_tls_data + at;
        static_tls_used = at + (total ? total : 1);
        if (callbacks) {
            u32 j;
            for (j = 0; j <= 64; j++) {
                if (!module_address(m, callbacks + (u64) j * 8, 8))return 0;
                u64 fn = rd64((u8 *) (callbacks + (u64) j * 8));
                if (!fn)break;
                if (j == 64 || !module_code(m, fn))return 0;
                tls_callbacks[i][j] = fn;
            }
            tls_callback_count[i] = j;
        }
    }
    *(void ***) (app_teb + 0x58) = static_tls_vector;
    print("Static TLS modules: ");
    number(slots);
    print("; bytes: ");
    number(static_tls_used);
    print("\n");
    return 1;
}

static void notify_tls(u32 i, u32 reason) {
    for (u32 j = 0; j < tls_callback_count[i]; j++)((void(*)(void *, u32, void *)) tls_callbacks[i][j])(
        modules[i].base, reason, 0);
}
