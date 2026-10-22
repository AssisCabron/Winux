/* Trusted native PCI DMA: dedicated coherent bounce pool, device ownership.
 * Only addresses inside this reserved aperture may enter device descriptors.
 * Identity-mapped low memory, one CPU, QEMU legacy VirtIO. This is NOT an
 * IOMMU: a malicious/untrusted PCI endpoint can bypass all software checks.
 * Real hardware DMA isolation requires VT-d/AMD-Vi domains and fault handling.
 */
#ifndef WINUX_DMA_H
#define WINUX_DMA_H
#define WX_DMA_REGIONS 12U
#define WX_DMA_POOL_BYTES (32U*1024U)
#define WX_DMA_TO_DEVICE 1U
#define WX_DMA_FROM_DEVICE 2U
typedef struct {
    const wx_pci_function *owner;
    u64 base,length;
    u32 direction;
    u32 generation;
} wx_dma_region;
static u8 wx_dma_pool[WX_DMA_POOL_BYTES] __attribute__((aligned(4096)));
static wx_dma_region wx_dma_regions[WX_DMA_REGIONS];
static u32 wx_dma_used,wx_dma_cursor;
static u32 wx_dma_locked; /* No registrations while VT-d context is live. */
#ifdef WINUX_INTEL_VTD_PREVIEW
static u32 wx_vtd_enabled;
#endif
/* Generation invalidates mappings even when a later probe reuses the same
 * physical pool addresses. This protects trusted native drivers from stale
 * descriptors, not malicious hardware; no IOMMU is active. */
static u32 wx_dma_generation=1;
static u32 wx_dma_rejected;
static int wx_dma_pool_bounds(u64 base,u64 bytes){
    u64 start=(u64)wx_dma_pool;
    return bytes && base>=start && base-start<WX_DMA_POOL_BYTES &&
           bytes<=WX_DMA_POOL_BYTES-(base-start) &&
           base<0x100000000ULL && bytes<=0x100000000ULL-base &&
           pg_low(base,bytes);
}
static int wx_dma_owned(const wx_pci_function *owner){
    if(!owner || !owner->claimed)return 0;
    for(u32 i=0;i<wx_pci_count;i++)if(owner==&wx_pci_devices[i])return 1;
    return 0;
}
static int wx_dma_register(const wx_pci_function *owner,
                           const void *buffer,u64 bytes,u32 direction){
    u64 base=(u64)buffer;
    if(!wx_dma_owned(owner) || !buffer ||
       !direction || (direction&~3U) || !wx_dma_pool_bounds(base,bytes) ||
       wx_dma_locked || wx_dma_used>=WX_DMA_REGIONS){wx_dma_rejected++;return 0;}
    for(u32 i=0;i<wx_dma_used;i++){
        const wx_dma_region *entry=&wx_dma_regions[i];
        if(base<entry->base+entry->length &&
           entry->base<base+bytes){wx_dma_rejected++;return 0;}
    }
    wx_dma_regions[wx_dma_used++]=(wx_dma_region){owner,base,bytes,direction,wx_dma_generation};
    return 1;
}
/* One coherent allocation owned by a PCI function, with explicit direction.
 * No arbitrary kernel pointer can be registered, including user frames,
 * kernel page tables, thread stacks or drivers' global .data. */
static void *wx_dma_alloc(const wx_pci_function *owner,u32 bytes,
                          u32 alignment,u32 direction){
    if(!wx_dma_owned(owner) || !bytes || !alignment || alignment>4096 ||
       (alignment&(alignment-1U))){wx_dma_rejected++;return 0;}
    u64 base=(u64)wx_dma_pool;
    u64 offset=((base+wx_dma_cursor+alignment-1ULL)&~((u64)alignment-1ULL))-base;
    if(offset>=WX_DMA_POOL_BYTES || bytes>WX_DMA_POOL_BYTES-offset){
        wx_dma_rejected++;return 0;
    }
    void *p=(void *)(base+offset);
    if(!wx_dma_register(owner,p,bytes,direction))return 0;
    wx_dma_cursor=(u32)(offset+bytes);
    pg_zero(p,bytes);
    return p;
}
static int wx_dma_bus_address(const wx_pci_function *owner,
                              const void *buffer,u64 bytes,u32 direction,
                              u32 generation,u64 *bus_address){
    u64 base=(u64)buffer;
    if(!wx_dma_owned(owner) || !buffer || !bus_address ||
#ifdef WINUX_INTEL_VTD_PREVIEW
       !wx_dma_locked || !wx_vtd_enabled ||
#endif
       !generation || generation!=wx_dma_generation ||
       !direction || (direction&~3U) ||
       !wx_dma_pool_bounds(base,bytes)){wx_dma_rejected++;return 0;}
    for(u32 i=0;i<wx_dma_used;i++){
        const wx_dma_region *entry=&wx_dma_regions[i];
        if(entry->owner==owner && entry->generation==generation &&
           (entry->direction&direction)==direction &&
           base>=entry->base && base-entry->base<=entry->length &&
           bytes<=entry->length-(base-entry->base)){
            *bus_address=base;return 1;
        }
    }
    wx_dma_rejected++;return 0;
}
/* Fail closed if the PCI function can still issue DMA transactions.
 * A completed reset/driver quiesce remains the caller's responsibility;
 * PCI COMMAND.BUS_MASTER=0 is a necessary, not sufficient, boundary. */
static int wx_dma_unregister_device(const wx_pci_function *owner){
    if(!wx_dma_owned(owner) || wx_dma_locked ||
       (wx_pci_config(owner->bus,owner->slot,owner->function,4)&4U)){
        wx_dma_rejected++;return 0;
    }
    u32 keep=0;
    for(u32 i=0;i<wx_dma_used;i++){
        wx_dma_region entry=wx_dma_regions[i];
        if(entry.owner==owner){
            pg_zero((void*)entry.base,entry.length);
            continue;
        }
        wx_dma_regions[keep++]=entry;
    }
    for(u32 i=keep;i<wx_dma_used;i++)wx_dma_regions[i]=(wx_dma_region){0};
    wx_dma_used=keep;
    if(!wx_dma_used){
        wx_dma_cursor=0;
        wx_dma_generation++;
        if(!wx_dma_generation)wx_dma_generation=1;
    }
    return 1;
}
static void wx_dma_report(void){
    print("DMA_STATUS_BEGIN\n");
    print("DMA_STATUS pool_bytes=");number(WX_DMA_POOL_BYTES);
    print(" reserved_bytes=");number(wx_dma_cursor);
    print(" mappings=");number(wx_dma_used);
    print(" generation=");number(wx_dma_generation);
    print(" rejected=");number(wx_dma_rejected);
#ifdef WINUX_INTEL_VTD_PREVIEW
    print(" iommu=");print(wx_vtd_enabled?"intel-vtd":"disabled");
#else
    print(" iommu=not-configured");
#endif
    print("\nDMA_STATUS_END\n");
}
#endif
