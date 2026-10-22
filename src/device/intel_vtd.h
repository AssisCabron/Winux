/* Experimental Intel VT-d legacy second-level translation (QEMU q35, one CPU).
 * No passthrough outside explicitly registered PCI functions.
 * Not a production IOMMU: no multiple DRHD / RMRR / ATS / hotplug / SMP.
 * Translation is never silently disabled after first activation.
 */
#ifndef WINUX_INTEL_VTD_H
#define WINUX_INTEL_VTD_H
#define WX_VTD_DOMAINS 4U
#define WX_VTD_BUSES 4U

typedef struct {
    u16 bdf;
    u16 did;
    u8 active;
    u64 pml4[512] __attribute__((aligned(4096)));
    u64 pdpt[512] __attribute__((aligned(4096)));
    u64 pd[512] __attribute__((aligned(4096)));
    u64 pt[2][512] __attribute__((aligned(4096)));
} wx_vtd_domain;

static u64 wx_vtd_root[512] __attribute__((aligned(4096)));
static u64 wx_vtd_context[WX_VTD_BUSES][512] __attribute__((aligned(4096)));
static u8 wx_vtd_bus_ids[WX_VTD_BUSES];
typedef struct {u8 bus,dev,fn;} wx_vtd_scope;
static wx_vtd_scope wx_vtd_scopes[32];
static u32 wx_vtd_scope_count,wx_vtd_include_all;
static u32 wx_vtd_bus_count;
static wx_vtd_domain wx_vtd_domains[WX_VTD_DOMAINS] __attribute__((aligned(4096)));
static volatile u8 *wx_vtd_mmio;
static u64 wx_vtd_cap, wx_vtd_ecap;
static u32 wx_vtd_present,wx_vtd_failed,wx_vtd_faults,wx_vtd_invalidations;
static u64 wx_vtd_last_fault_addr, wx_vtd_last_fault_hi;
static u8 wx_vtd_last_fault_valid;
static u32 wx_vtd_read32(u32 reg) { return *(volatile u32 *) (wx_vtd_mmio + reg); }
static u64 wx_vtd_read64(u32 reg) { return *(volatile u64 *) (wx_vtd_mmio + reg); }

static void wx_vtd_write32(u32 reg, u32 v) {
    *(volatile u32 *) (wx_vtd_mmio + reg) = v;
    __asm__ volatile("":::"memory");
}

static void wx_vtd_write64(u32 reg, u64 v) {
    *(volatile u64 *) (wx_vtd_mmio + reg) = v;
    __asm__ volatile("":::"memory");
}

static int wx_acpi_range(u64 addr, u32 bytes) {
    return addr >= 0x1000 && bytes >= 4 && addr < 0x100000000ULL &&
           bytes <= 0x100000000ULL - addr && pg_low(addr, bytes);
}

static u32 wx_acpi_u32(const u8 *p) { return (u32) p[0] | ((u32) p[1] << 8) | ((u32) p[2] << 16) | ((u32) p[3] << 24); }
static u64 wx_acpi_u64(const u8 *p) { return (u64) wx_acpi_u32(p) | ((u64) wx_acpi_u32(p + 4) << 32); }

static int wx_acpi_sig(const u8 *p, const char *s, u32 n) {
    for (u32 i = 0; i < n; i++)if (p[i] != (u8) s[i])return 0;
    return 1;
}

static int wx_acpi_sum(const u8 *p, u32 n) {
    u8 sum = 0;
    for (u32 i = 0; i < n; i++)sum = (u8)(sum + p[i]);
    return sum == 0;
}

static const u8 *wx_acpi_table(u64 address, const char *sig) {
    if (!wx_acpi_range(address, 36))return 0;
    const u8 *table = (const u8 *) address;
    if (!wx_acpi_sig(table, sig, 4))return 0;
    u32 len = wx_acpi_u32(table + 4);
    if (len < 36 || len > 65536 || !wx_acpi_range(address, len) || !wx_acpi_sum(table, len))return 0;
    return table;
}

static int wx_vtd_discover(void) {
    print("VTD_DIAG rsdp=");print_hex(boot?(u64)boot->rsdp:0);print("\n");
    if (!boot || !boot->rsdp)return 0;
    u64 rp = (u64) boot->rsdp;
    if (!wx_acpi_range(rp, 36))return 0;
    const u8 *rsdp = (const u8 *) rp;
    if (!wx_acpi_sig(rsdp, "RSD PTR ", 8) || !wx_acpi_sum(rsdp, 20) ||
        rsdp[15] < 2 || wx_acpi_u32(rsdp + 20) < 36 ||
        !wx_acpi_sum(rsdp, 36))
        return 0;
    print("VTD_DIAG rsdp-valid xsdt=");print_hex(wx_acpi_u64(rsdp+24));print("\n");
    const u8 *xsdt = wx_acpi_table(wx_acpi_u64(rsdp + 24), "XSDT");
    if (!xsdt)return 0;
    u32 count = (wx_acpi_u32(xsdt + 4) - 36) / 8;
    print("VTD_DIAG xsdt-valid entries=");number(count);print("\n");
    for (u32 i = 0; i < count; i++) {
        u64 addr=wx_acpi_u64(xsdt + 36 + 8*i);
        if(wx_acpi_range(addr,36)){
          const u8 *h=(const u8*)addr;
          print("VTD_DIAG table ");number(i);print(" sig=");
          for(unsigned c=0;c<4;c++)console_char((char)h[c]);
          print(" len=");number(wx_acpi_u32(h+4));print("\n");
        }
        const u8 *dmar = wx_acpi_table(wx_acpi_u64(xsdt + 36 + 8 * i), "DMAR");
        if (!dmar)continue;
        u32 len = wx_acpi_u32(dmar + 4);
        if (len < 48)return 0;
        u32 drhd = 0;
        u64 base = 0;
        for (u32 p = 48; p < len;) {
            if (len - p < 4)return 0;
            u32 type = (u32) dmar[p] | ((u32) dmar[p + 1] << 8);
            u32 size = (u32) dmar[p + 2] | ((u32) dmar[p + 3] << 8);
            if (size < 4 || size > len - p)return 0;
            print("VTD_DIAG entry type=");number(type);print(" size=");number(size);print(" flag=");number(dmar[p+4]);print("\n");
            if (type == 0) {
                if (size < 16 || (++drhd) > 1)return 0; /* multi-DRHD not supported */
                u16 segment = (u16) dmar[p + 6] | ((u16) dmar[p + 7] << 8);
                if(segment)return 0;
                wx_vtd_include_all=(dmar[p+4]&1U)!=0;
                wx_vtd_scope_count=0;
                print("VTD_DIAG drhd_flags=");number(dmar[p+4]);print(" scopes=");number(size-16);print("\n");
                for(u32 s=p+16;s+6<=p+size;){
                    u32 sl=dmar[s+1];if(sl<8||s+sl>p+size)return 0;
                    print("VTD_SCOPE type=");number(dmar[s]);print(" len=");number(sl);
                    print(" bus=");number(dmar[s+5]);
                    print(" dev=");number(dmar[s+6]);print(" fn=");number(dmar[s+7]);print("\n");
                    if(dmar[s]==1 && sl==8){
                        if(wx_vtd_scope_count>=32)return 0;
                        wx_vtd_scopes[wx_vtd_scope_count++]=(wx_vtd_scope){dmar[s+5],dmar[s+6],dmar[s+7]};
                    }
                    s+=sl;
                }
                base = wx_acpi_u64(dmar + p + 8);
            } else if (type == 1) { return 0; } /* RMRR not mapped; refuse */
            p += size;
        }
        if (drhd != 1 || !base || base & 4095ULL || !wx_acpi_range(base, 4096))return 0;
        wx_vtd_mmio = (volatile u8 *) base;
        wx_vtd_cap = wx_vtd_read64(0x08);
        wx_vtd_ecap = wx_vtd_read64(0x10);
        /* QEMU legacy 4-level translation (SAGAW 48, bit 2). */
        print("VTD_DIAG cap=");print_hex(wx_vtd_cap);print(" ecap=");print_hex(wx_vtd_ecap);print("\n");
        if (!((wx_vtd_cap >> 8) & 4ULL))return 0;
        u32 iotlb = ((u32)((wx_vtd_ecap >> 8) & 0x3ffU)) * 16U;
        if (iotlb < 0x80 || iotlb > 0xf00)return 0;
        wx_vtd_present = 1;
        print("VTD_DMAR_OK: ACPI XSDT/DMAR checksum, DRHD scoped, segment=0 MMIO=");
        print_hex(base);
        print("\n");
        return 1;
    }
    return 0;
}

static int wx_vtd_poll(u32 reg, u32 mask, int set) {
    for (u32 i = 0; i < 1000000; i++) {
        u32 s = wx_vtd_read32(reg);
        if (!!(s & mask) == !!set)return 1;
        __asm__ volatile("pause");
    }
    return 0;
}

static int wx_vtd_poll64(u32 reg, u64 mask) {
    for (u32 i = 0; i < 1000000; i++) {
        if (!(wx_vtd_read64(reg) & mask))return 1;
        __asm__ volatile("pause");
    }
    return 0;
}

static int wx_vtd_flush(void) {
    if (!wx_vtd_present)return 0;
    u32 offset = ((u32)((wx_vtd_ecap >> 8) & 0x3ffU)) * 16U;
    /* Global context-cache then IOTLB invalidation; synchronous polling. */
    wx_vtd_write64(0x28, (1ULL << 63) | (1ULL << 61));
    if (!wx_vtd_poll64(0x28, 1ULL << 63))return 0;
    wx_vtd_write64(offset + 8, (1ULL << 63) | (1ULL << 60));
    if (!wx_vtd_poll64(offset + 8, 1ULL << 63))return 0;
    wx_vtd_invalidations++;
    return 1;
}

static int wx_vtd_iotlb_invalidate(void){
    if(!wx_vtd_enabled)return 0;
    u32 off=((u32)((wx_vtd_ecap>>8)&0x3ffU))*16U;
    wx_vtd_write64(off+8,(1ULL<<63)|(1ULL<<60));
    if(!wx_vtd_poll64(off+8,1ULL<<63))return 0;
    wx_vtd_invalidations++;
    return 1;
}
static u64 wx_vtd_iova_for(const wx_pci_function *f, const void *p, u64 len) {
    if (!f || !len || !wx_dma_pool_bounds((u64) p, len))return 0;
    u16 bdf = ((u16) f->bus << 8) | ((u16) f->slot << 3) | f->function;
    for (u32 i = 0; i < WX_VTD_DOMAINS; i++)
        if (wx_vtd_domains[i].active && wx_vtd_domains[i].bdf == bdf)
            return (u64) p; /* identity IOVA, translated by IOMMU, not bypass */
    return 0;
}

static int wx_vtd_map_pool(wx_vtd_domain *dom, const wx_pci_function *owner) {
    u64 start = (u64) wx_dma_pool;
    u64 last = start + WX_DMA_POOL_BYTES - 1;
    if (last >= 0x40000000ULL || (last >> 21) > (start >> 21) + 1)return 0;
    u32 first = (u32)(start >> 21);
    dom->pml4[0] = (u64) dom->pdpt | 3;
    dom->pdpt[0] = (u64) dom->pd | 3;
    dom->pd[first] = (u64) dom->pt[0] | 3;
    if ((last >> 21) != first)dom->pd[first + 1] = (u64) dom->pt[1] | 3;
    for (u32 i = 0; i < wx_dma_used; i++) {
        const wx_dma_region *region = &wx_dma_regions[i];
        if (region->owner != owner)return 0; /* shared pool cannot span owners */
        u64 lo = region->base & ~4095ULL, hi = (region->base + region->length + 4095ULL) & ~4095ULL;
        if (!region->length || hi < lo)return 0;
        for (u64 p = lo; p < hi; p += 4096) {
            u32 which = (u32)((p >> 21) - first);
            if (which > 1)return 0;
            u64 *pte = &dom->pt[which][(p >> 12) & 511];
            u64 flags = 0;
            if (region->direction & WX_DMA_TO_DEVICE)flags |= 1;
            if (region->direction & WX_DMA_FROM_DEVICE)flags |= 2;
            *pte = p | ((*pte) & 3ULL) | flags;
        }
    }
    return 1;
}

static int wx_vtd_attach(const wx_pci_function *owner) {
    if (!wx_vtd_present||wx_vtd_failed)return 0;
    if (!owner || wx_dma_locked || !wx_dma_used)return 0;
    u16 bdf = ((u16) owner->bus << 8) | ((u16) owner->slot << 3) | owner->function;
    if(!wx_vtd_include_all){
        int covered=0;
        for(u32 n=0;n<wx_vtd_scope_count;n++)if(wx_vtd_scopes[n].bus==owner->bus &&
            wx_vtd_scopes[n].dev==owner->slot && wx_vtd_scopes[n].fn==owner->function)covered=1;
        if(!covered){print("VTD_SCOPE_DENIED: PCI BDF not listed in ACPI DRHD\n");return 0;}
    }
    wx_vtd_domain *dom = 0;
    for (u32 i = 0; i < WX_VTD_DOMAINS; i++)if (!wx_vtd_domains[i].active) {
        dom = &wx_vtd_domains[i];
        break;
    }
    if (!dom)return 0;
    u32 bus_index = WX_VTD_BUSES;
    for (u32 i = 0; i < wx_vtd_bus_count; i++)if (wx_vtd_bus_ids[i] == owner->bus)bus_index = i;
    if (bus_index == WX_VTD_BUSES) {
        if (wx_vtd_bus_count >= WX_VTD_BUSES)return 0;
        bus_index = wx_vtd_bus_count++;
        wx_vtd_bus_ids[bus_index] = owner->bus;
        pg_zero(wx_vtd_context[bus_index], 4096);
        wx_vtd_root[owner->bus * 2] = (u64) wx_vtd_context[bus_index] | 1;
    }
    pg_zero(dom, sizeof(*dom));
    dom->bdf = bdf;
    dom->did = (u16)(dom - wx_vtd_domains) + 1;
    if (!wx_vtd_map_pool(dom, owner))return 0;
    u32 devfn = (owner->slot << 3) | owner->function;
    u64 *entry = &wx_vtd_context[bus_index][devfn * 2];
    if (entry[0] || entry[1])return 0;
    /* Context: present, translation type 0, second-level page pointer;
     * DID in bits 8..23, AW=2 for four-level 48bit. */
    entry[1] = ((u64) dom->did << 8) | 2;
    __asm__ volatile("":::"memory");
    entry[0] = (u64) dom->pml4 | 1;
    dom->active = 1;
    if (!wx_vtd_enabled) {
        wx_vtd_write64(0x20, (u64) wx_vtd_root); /* RTADDR legacy */
        wx_vtd_write32(0x18, 1U << 30); /* SRTP */
        if (!wx_vtd_poll(0x1c, 1U << 30, 1))goto fail;
        wx_vtd_write32(0x18, 1U << 31); /* TE */
        if (!wx_vtd_poll(0x1c, 1U << 31, 1))goto fail;
        wx_vtd_enabled = 1;
    }
    if (!wx_vtd_flush())goto fail;
    wx_dma_locked = 1;
    print("VTD_DOMAIN_OK: legacy second-level page tables, BDF=");
    print_hex(bdf);
    print(" domain=");
    number(dom->did);
    print("\n");
    return 1;
fail:
    wx_vtd_failed = 1;
    /* Fail closed: never return to untranslated DMA after enabling TE. */
    return 0;
}

static int wx_vtd_detach(const wx_pci_function *owner) {
    if (!wx_vtd_present)return 1;
    if (!owner || (wx_pci_config(owner->bus, owner->slot, owner->function, 4) & 4U))return 0;
    u16 bdf = ((u16) owner->bus << 8) | ((u16) owner->slot << 3) | owner->function;
    for (u32 i = 0; i < WX_VTD_DOMAINS; i++) {
        wx_vtd_domain *dom = &wx_vtd_domains[i];
        if (!dom->active || dom->bdf != bdf)continue;
        u32 j = 0;
        while (j < wx_vtd_bus_count && wx_vtd_bus_ids[j] != owner->bus)j++;
        if (j == wx_vtd_bus_count)return 0;
        u32 devfn = (owner->slot << 3) | owner->function;
        u64 *entry = &wx_vtd_context[j][devfn * 2];
        entry[0] = entry[1] = 0;
        __asm__ volatile("":::"memory");
        if (!wx_vtd_flush())return 0;
        dom->active = 0;
        wx_dma_locked = 0;
        return 1;
    }
    return 0;
}

static int wx_vtd_fault_snapshot(void) {
    if (!wx_vtd_enabled)return 0;
    u32 status = wx_vtd_read32(0x34);
    if (!(status & 3U))return 0;
    u32 fro = (u32)((wx_vtd_cap >> 24) & 0x3ffU) * 16U;
    if (fro < 0x80 || fro > 0xfc0)return 0;
    wx_vtd_last_fault_addr = wx_vtd_read64(fro);
    wx_vtd_last_fault_hi = wx_vtd_read64(fro + 8);
    wx_vtd_last_fault_valid = (u8)((wx_vtd_last_fault_hi >> 63) & 1U);
    if (wx_vtd_last_fault_valid)wx_vtd_faults++;
    wx_vtd_write64(fro + 8, 1ULL << 63); /* W1C valid record */
    wx_vtd_write32(0x34, status & 0x3fU); /* W1C fault status */
    return wx_vtd_last_fault_valid;
}

static void wx_vtd_report(void) {
    print("VTD_STATUS present=");
    number(wx_vtd_present);
    print(" enabled=");
    number(wx_vtd_enabled);
    print(" failed=");
    number(wx_vtd_failed);
    print(" domains=");
    u32 total = 0;
    for (u32 i = 0; i < WX_VTD_DOMAINS; i++)total += wx_vtd_domains[i].active;
    number(total);
    print(" invalidations=");
    number(wx_vtd_invalidations);
    print(" faults=");
    number(wx_vtd_faults);
    print("\n");
}
#endif
