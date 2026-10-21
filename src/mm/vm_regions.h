/* Original single-address-space region manager. Internal kernel API only.
 * Reservations use 64 KiB alignment; commits use 4 KiB zeroed pool pages.
 * Not an NT syscall implementation, demand paging, or a security boundary. */
typedef struct {
    u64 base, size;
    void *pages[VM_REGION_PAGES];
    u8 protection[VM_REGION_PAGES];
    u32 allocation_protect;
} vm_region;

static vm_region vm_regions[16];

static vm_region *vm_find(u64 base, u64 size) {
    if (!size)return 0;
    for (unsigned i = 0; i < 16; i++) {
        vm_region *r = &vm_regions[i];
        if (r->size && base >= r->base && base - r->base < r->size && size <= r->size - (base - r->base))return r;
    }
    return 0;
}

static int vm_span(u64 base, u64 size) { return size && !(base & 4095) && !(size & 4095); }

static u64 vm_reserve(u64 size) {
    if (!size || size > VM_REGION_PAGES * 4096)return 0;
    size = (size + 4095) & ~4095ULL;
    for (unsigned i = 0; i < 16; i++)
        if (!vm_regions[i].size) {
            /* Metadata slots are independent of addresses after a partial release. */
            const u64 end = VM_WINDOW_END;
            for (u64 base = VM_WINDOW_START; base <= end - size; base += 65536) {
                int conflict = 0;
                for (unsigned j = 0; j < 16; j++) {
                    vm_region *r = &vm_regions[j];
                    if (r->size && base < r->base + r->size && r->base < base + size) {
                        conflict = 1;
                        break;
                    }
                }
                if (conflict)continue;
                vm_region *r = &vm_regions[i];
                pg_zero(r, sizeof(*r));
                r->base = base;
                r->size = size;
                r->allocation_protect = 4;
                return base;
            }
            return 0;
        }
    return 0;
}

/* Protection bits: 1 readable, 2 writable, 4 executable. No PAGE_GUARD. */
static int vm_protection(unsigned p) { return p == 1 || p == 3 || p == 5 || p == 7; }

static int vm_commit(u64 base, u64 size, unsigned protection) {
    vm_region *r = vm_find(base, size);
    if (!r || !vm_span(base, size) || !vm_protection(protection))return 0;
    unsigned first = (unsigned) ((base - r->base) / 4096), count = (unsigned) (size / 4096);
    u8 added[(VM_REGION_PAGES + 7) / 8] = {0};
    for (unsigned j = 0; j < count; j++) {
        unsigned i = first + j;
        if (r->pages[i])continue; /* Preserve bytes; update protection after all allocations succeed. */
        void *p = page_alloc();
        if (!p)goto rollback;
        if (!pg_map(r->base + (u64) i * 4096, (u64) p, !!(protection & 2), !!(protection & 4))) {
            page_free(p);
            goto rollback;
        }
        r->pages[i] = p;
        r->protection[i] = (u8) protection;
        added[j / 8] |= (u8)(1U << (j % 8));
    }
    for (unsigned j = 0; j < count; j++) {
        unsigned i = first + j;
        pg_protect(r->base + (u64) i * 4096, !!(protection & 2), !!(protection & 4));
        r->protection[i] = (u8) protection;
    }
    return 1;
rollback:
    for (unsigned j = 0; j < count; j++)if (added[j / 8] & (1U << (j % 8))) {
        unsigned i = first + j;
        pg_unmap(r->base + (u64) i * 4096);
        page_free(r->pages[i]);
        r->pages[i] = 0;
        r->protection[i] = 0;
    }
    return 0;
}

static int vm_decommit(u64 base, u64 size) {
    vm_region *r = vm_find(base, size);
    if (!r || !vm_span(base, size))return 0;
    unsigned first = (unsigned) ((base - r->base) / 4096), count = (unsigned) (size / 4096);
    for (unsigned j = 0; j < count; j++) {
        unsigned i = first + j;
        if (r->pages[i]) {
            pg_unmap(r->base + (u64) i * 4096);
            page_free(r->pages[i]);
            r->pages[i] = 0;
            r->protection[i] = 0;
        }
    }
    return 1;
}

static int vm_protect(u64 base, u64 size, unsigned protection) {
    vm_region *r = vm_find(base, size);
    if (!r || !vm_span(base, size) || !vm_protection(protection))return 0;
    unsigned first = (unsigned) ((base - r->base) / 4096), count = (unsigned) (size / 4096);
    /* Validate the complete range before changing any permissions. */
    for (unsigned j = 0; j < count; j++)if (!r->pages[first + j])return 0;
    for (unsigned j = 0; j < count; j++) {
        unsigned i = first + j;
        pg_protect(r->base + (u64) i * 4096, !!(protection & 2), !!(protection & 4));
        r->protection[i] = (u8) protection;
    }
    return 1;
}

static int vm_release(u64 base) {
    vm_region *r = vm_find(base, 1);
    if (!r || r->base != base)return 0;
    if (!vm_decommit(base, r->size))return 0;
    pg_zero(r, sizeof(*r));
    return 1;
}

static int vm_release_span(u64 base, u64 size) {
    vm_region *r = vm_find(base, size);
    if (!r || !vm_span(base, size))return 0;
    unsigned left = (unsigned) ((base - r->base) / 4096), removed = (unsigned) (size / 4096), total = (unsigned) (
        r->size / 4096), right = total - left - removed;
    vm_region *tail = 0;
    if (left && right) {
        for (unsigned i = 0; i < 16; i++)if (!vm_regions[i].size) {
            tail = &vm_regions[i];
            break;
        }
        if (!tail)return 0;
    }
    if (!vm_decommit(base, size))return 0;
    if (!left && !right) {
        pg_zero(r, sizeof(*r));
        return 1;
    }
    if (left && right) {
        pg_zero(tail, sizeof(*tail));
        tail->base = base + size;
        tail->size = (u64) right * 4096;
        tail->allocation_protect = r->allocation_protect;
        for (unsigned i = 0; i < right; i++) {
            tail->pages[i] = r->pages[left + removed + i];
            tail->protection[i] = r->protection[left + removed + i];
        }
    }
    if (!left) {
        for (unsigned i = 0; i < right; i++) {
            r->pages[i] = r->pages[removed + i];
            r->protection[i] = r->protection[removed + i];
        }
        r->base = base + size;
        r->size = (u64) right * 4096;
    } else r->size = (u64) left * 4096;
    for (unsigned i = (unsigned) (r->size / 4096); i < VM_REGION_PAGES; i++) {
        r->pages[i] = 0;
        r->protection[i] = 0;
    }
    return 1;
}

/* x64 MemoryBasicInformation layout. Only our managed window is queryable. */
typedef struct {
    u64 base, allocation;
    u32 allocation_protect;
    u16 partition, pad;
    u64 region_size;
    u32 state, protect, type, end_pad;
} vm_basic_info;

_Static_assert (
sizeof
(vm_basic_info)
==
48
,
"x64 memory information size"
);
static u32 vm_windows_protect(unsigned p) { return p == 1 ? 2 : p == 3 ? 4 : p == 5 ? 0x20 : p == 7 ? 0x40 : 0; }

static int vm_query(u64 address, vm_basic_info *info) {
    const u64 start = VM_WINDOW_START, end = VM_WINDOW_END;
    if (!info || address < start || address >= end)return 0;
    u64 base = address & ~4095ULL;
    pg_zero(info, sizeof(*info));
    info->base = base;
    vm_region *r = vm_find(base, 1);
    if (!r) {
        u64 limit = end;
        for (unsigned i = 0; i < 16; i++)if (vm_regions[i].size && vm_regions[i].base > base && vm_regions[i].base <
                                             limit)limit = vm_regions[i].base;
        info->state = 0x10000;
        info->region_size = limit - base;
        return 1;
    }
    unsigned first = (unsigned) ((base - r->base) / 4096), last = first + 1, total = (unsigned) (r->size / 4096);
    int committed = r->pages[first] != 0;
    u8 protection = r->protection[first];
    while (last < total && (r->pages[last] != 0) == committed && (!committed || r->protection[last] == protection))last
            ++;
    info->allocation = r->base;
    info->allocation_protect = r->allocation_protect;
    info->state = committed ? 0x1000 : 0x2000;
    info->protect = committed ? vm_windows_protect(protection) : 0;
    info->region_size = (u64)(last - first) * 4096;
    info->type = 0x20000;
    return 1;
}

static int vm_observe(const char *label, u64 allocation, u64 offset, int free_state) {
    vm_basic_info q;
    if (!vm_query(allocation + offset, &q))return 0;
    if (free_state) { if (q.state != 0x10000)return 0; } else if (q.allocation != allocation)return 0;
    print("VM_QUERY ");
    print(label);
    print(" ");
    if (free_state)number(q.state);
    else {
        u64 values[] = {q.base - allocation, q.region_size, q.state, q.protect, q.allocation_protect, q.type};
        for (unsigned i = 0; i < 6; i++) {
            if (i)print(",");
            number(values[i]);
        }
    }
    print("\n");
    return 1;
}

static int vm_query_test(void) {
    u64 a = vm_reserve(16384);
    if (!a)return 0;
    int ok = 1;
    vm_basic_info q;
    if (vm_query(0, &q) || vm_query(VM_WINDOW_END, &q) || vm_query(a, 0))ok = 0;
    if (!vm_observe("reserve", a, 123, 0) || !vm_observe("reserve-interior", a, 4103, 0))ok = 0;
    if (!vm_commit(a + 4096, 8192, 3)) {
        ok = 0;
        goto done;
    }
    if (!vm_observe("prefix", a, 0, 0) || !vm_observe("commit", a, 4097, 0) || !
        vm_observe("commit-interior", a, 8193, 0) || !vm_observe("suffix", a, 12288, 0))ok = 0;
    if (!vm_protect(a + 8192, 4096, 1) || !vm_observe("split-write", a, 4096, 0) || !vm_observe(
            "split-read", a, 8192, 0))ok = 0;
    if (!vm_decommit(a + 4096, 8192) || !vm_observe("decommit", a, 0, 0))ok = 0;
    if (!vm_commit(a, 16384, 3) || !vm_observe("recommit", a, 0, 0))ok = 0;
done:
    if (!vm_release(a) || !vm_observe("release", a, 0, 1))ok = 0;
    if (!pool_test())ok = 0;
    return ok;
}

static int vm_test(void) {
    u64 a = vm_reserve(12288), b = vm_reserve(PAGE_POOL_COUNT * 4096);
    int ok = 1;
    if (!a || !b) {
        if (a)vm_release(a);
        if (b)vm_release(b);
        return 0;
    }
    vm_region *r = vm_find(a, 12288);
    if (!r || r->pages[0] || vm_commit(a + 1, 4096, 3) || vm_commit(a, 16384, 3) || vm_commit(a, 4096, 0))ok = 0;
    if (!vm_commit(a, 8192, 3)) {
        ok = 0;
        goto done;
    }
    volatile u8 *p = (volatile u8 *) a;
    if (p[0] || p[4096])ok = 0;
    p[0] = 93;
    p[4096] = 71;
    if (!vm_commit(a, 8192, 1) || p[0] != 93 || r->protection[0] != 1)ok = 0;
    if (vm_protect(a, 12288, 1) || r->protection[0] != 1)ok = 0;
    if (!vm_protect(a, 4096, 1) || (*pg_leaf(a, 0) & PG_WRITE) || !(*pg_leaf(a, 0) & PG_NX))ok = 0;
    if (!vm_protect(a, 4096, 3))ok = 0;
    /* Two pages occupied: a pool-sized commit must roll back, preserving a. */
    if (vm_commit(b, PAGE_POOL_COUNT * 4096, 3) || p[0] != 93 || p[4096] != 71)ok = 0;
    for (unsigned i = 0; i < PAGE_POOL_COUNT; i++)if (vm_find(b, 1)->pages[i])ok = 0;
    if (!vm_decommit(a, 4096) || !vm_decommit(a, 4096) || !vm_commit(a, 4096, 3))ok = 0;
    if (p[0] || p[4096] != 71 || vm_release(a + 4096))ok = 0;
done:
    if (!vm_release(a) || !vm_release(b) || vm_release(a))ok = 0;
    /* REA observed reserve ceiling in this growth path; commit a 3 MiB payload. */
    a = vm_reserve(0xfd0000);
    if (!a)return 0;
    if (!vm_commit(a, 3 * 1024 * 1024, 3)) {
        vm_release(a);
        return 0;
    }
    p = (volatile u8 *) a;
    for (unsigned i = 0; i < 768; i++) {
        if (p[i * 4096] || p[i * 4096 + 4095])ok = 0;
        p[i * 4096] = (u8) i;
        p[i * 4096 + 4095] = (u8)(i ^ 0xa5);
    }
    for (unsigned i = 0; i < 768; i++)if (p[i * 4096] != (u8) i || p[i * 4096 + 4095] != (u8)(i ^ 0xa5))ok = 0;
    vm_basic_info q;
    if (!vm_query(a, &q) || q.region_size != 3 * 1024 * 1024 || q.state != 0x1000)ok = 0;
    if (!vm_release(a))ok = 0;
    if (ok)print("VM_LARGE_OK: reserve 0xfd0000, commit 3 MiB, page data preserved\n");
    if (!pool_test())ok = 0;
    return ok;
}
