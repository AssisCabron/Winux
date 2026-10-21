/* Registered DMA-buffer boundary for trusted native drivers only.
 * Identity bus address is valid in the current below-4GiB QEMU mapping.
 * This registry protects Winux API callers from accidental bad pointers.
 * It DOES NOT constrain a malicious PCI function: IOMMU is not enabled.
 */
#ifndef WINUX_DMA_H
#define WINUX_DMA_H
#define WX_DMA_REGIONS 12
#define WX_DMA_TO_DEVICE 1U
#define WX_DMA_FROM_DEVICE 2U
typedef struct {
    const wx_pci_function *owner;
    u64 base,length;
    u32 direction;
} wx_dma_region;
static wx_dma_region wx_dma_regions[WX_DMA_REGIONS];
static u32 wx_dma_used;
static int wx_dma_register(const wx_pci_function *owner,
                           const void *buffer,u64 bytes,u32 direction){
    u64 base=(u64)buffer;
    if(!owner || !owner->claimed || !buffer || !bytes ||
       !direction || (direction & ~3U) ||
       base>=0x100000000ULL || bytes>0x100000000ULL-base ||
       !pg_low(base,bytes) || wx_dma_used>=WX_DMA_REGIONS)return 0;
    for(u32 i=0;i<wx_dma_used;i++){
        wx_dma_region *r=&wx_dma_regions[i];
        if(base<r->base+r->length&&r->base<base+bytes)return 0;
    }
    wx_dma_regions[wx_dma_used++]=(wx_dma_region){owner,base,bytes,direction};
    return 1;
}
static int wx_dma_bus_address(const wx_pci_function *owner,
                              const void *buffer,u64 bytes,u32 direction,
                              u64 *bus_address){
    u64 base=(u64)buffer;
    if(!owner||!owner->claimed||!buffer||!bus_address||!bytes ||
       !direction||(direction &~3U)||
       base>=0x100000000ULL||bytes>0x100000000ULL-base)return 0;
    for(u32 i=0;i<wx_dma_used;i++){
        const wx_dma_region *r=&wx_dma_regions[i];
        if(r->owner==owner&&(r->direction&direction)==direction &&
           base>=r->base && base-r->base<=r->length &&
           bytes<=r->length-(base-r->base)){
            *bus_address=base;return 1;
        }
    }
    return 0;
}
static void wx_dma_unregister_device(const wx_pci_function *owner){
    u32 write=0;
    for(u32 i=0;i<wx_dma_used;i++){
        if(wx_dma_regions[i].owner==owner)continue;
        wx_dma_regions[write++]=wx_dma_regions[i];
    }
    for(u32 i=write;i<wx_dma_used;i++)wx_dma_regions[i]=(wx_dma_region){0};
    wx_dma_used=write;
}
#endif
