/* Legacy virtio-blk PCI transport, first REAL post-ExitBootServices backend.
 * This preview supports ONE serialised read-only queue and QEMU legacy PCI
 * IDs 1af4:1001. It does not claim Windows Storport, AHCI, NVMe or modern VirtIO.
 * DMA currently uses identity-mapped, below-4GiB static kernel buffers.
 * Protect real hardware with IOMMU policy before enabling arbitrary drivers. */
#ifndef WINUX_VIRTIO_BLK_LEGACY_H
#define WINUX_VIRTIO_BLK_LEGACY_H
#define WX_VIRTQ_MAX 256U
#define WX_VIRTQ_BYTES 16384U
#define WX_VQ_NEXT 1U
#define WX_VQ_WRITE 2U
typedef struct __attribute__((packed)) {
    u64 address;
    u32 length;
    u16 flags;
    u16 next;
} wx_vq_desc;
typedef struct __attribute__((packed)) {
    u32 type;
    u32 reserved;
    u64 sector;
} wx_blk_req;

static struct {
    u16 port;
    u16 queue_size;
    u16 avail_index;
    u16 used_index;
    int initialized;
    u32 initial_pci_command;
    u32 dma_generation;
} wx_virtio_blk;

static u8 *wx_vq_memory;
static wx_blk_req *wx_request;
static u8 *wx_sector_buffer;
static volatile u8 *wx_request_status;

static wx_pci_function *wx_virtio_owner;
#ifdef WINUX_INTEL_VTD_PREVIEW
static u64 wx_virtio_illegal_dma_iova;
#endif
static inline void wx_io_barrier(void){__asm__ volatile("":::"memory");}

/* One hardware queue: zero-copied descriptor metadata, bounced 512-byte sector. */
static int wx_virtio_read_one(wx_block_device *device,u64 lba,void *destination) {
    if(!wx_virtio_blk.initialized || !device || !destination || lba>=device->capacity_sectors)return 0;
    u16 n=wx_virtio_blk.queue_size,port=wx_virtio_blk.port;
    volatile wx_vq_desc *desc=(volatile wx_vq_desc*)wx_vq_memory;
    volatile u16 *avail=(volatile u16*)(wx_vq_memory+16*n);
    u32 used_offset=((16U*n+6U+2U*n+4095U)&~4095U);
    volatile u16 *used=(volatile u16*)(wx_vq_memory+used_offset);
    if(used_offset+6U+8U*n>WX_VIRTQ_BYTES)return 0;
    u64 req_bus=0,sector_bus=0,status_bus=0;
    if(!wx_dma_bus_address(wx_virtio_owner,wx_request,sizeof(wx_blk_req),WX_DMA_TO_DEVICE,wx_virtio_blk.dma_generation,&req_bus) ||
       !wx_dma_bus_address(wx_virtio_owner,wx_sector_buffer,WX_BLOCK_SECTOR_BYTES,WX_DMA_FROM_DEVICE,wx_virtio_blk.dma_generation,&sector_bus) ||
       !wx_dma_bus_address(wx_virtio_owner,(const void*)wx_request_status,1,WX_DMA_FROM_DEVICE,wx_virtio_blk.dma_generation,&status_bus))return 0;
    *wx_request=(wx_blk_req){0U,0U,lba}; /* VIRTIO_BLK_T_IN */
    *wx_request_status=0xff;
#ifdef WINUX_INTEL_VTD_PREVIEW
    if(wx_virtio_illegal_dma_iova)sector_bus=wx_virtio_illegal_dma_iova;
#endif
    desc[0]=(wx_vq_desc){req_bus,16U,WX_VQ_NEXT,1U};
    desc[1]=(wx_vq_desc){sector_bus,WX_BLOCK_SECTOR_BYTES,WX_VQ_NEXT|WX_VQ_WRITE,2U};
    desc[2]=(wx_vq_desc){status_bus,1U,WX_VQ_WRITE,0U};
    avail[2+(wx_virtio_blk.avail_index%n)]=0;
    wx_io_barrier();
    wx_virtio_blk.avail_index++;
    avail[1]=wx_virtio_blk.avail_index;
    wx_io_barrier();
    wx_out16(port+0x10,0); /* notify queue 0 */
    unsigned tries=0;
    while(used[1]!=wx_virtio_blk.avail_index && tries++<10000000U)
        __asm__ volatile("pause":::"memory");
    wx_io_barrier();
    if(used[1]!=wx_virtio_blk.avail_index || *wx_request_status!=0)return 0;
    wx_virtio_blk.used_index=used[1];
    for(unsigned i=0;i<WX_BLOCK_SECTOR_BYTES;i++)((u8*)destination)[i]=wx_sector_buffer[i];
    return 1;
}
static int wx_virtio_read(wx_block_device *device,u64 lba,u32 sectors,void *out) {
    if(!device || !out || sectors>128U)return 0;
    for(u32 i=0;i<sectors;i++)
        if(!wx_virtio_read_one(device,lba+i,(u8*)out+i*WX_BLOCK_SECTOR_BYTES))return 0;
    return 1;
}

/* PCI discovery is hardware-driven, not tied to a QEMU bus/slot. */
/* Called by the common PCI binding mechanism only after an exact match. */
static int wx_virtio_probe(wx_pci_function *function){
#ifdef WINUX_INTEL_VTD_PREVIEW
    (void)function;return 0; /* legacy DMA bypasses QEMU vIOMMU */
#endif
    if(!function || function->vendor!=0x1af4U || function->device!=0x1001U)return 0;
    u32 bar=function->bars[0];
    if(!(bar&1U) || (bar&~3U)>0xff00U || (bar&~3U)<0x100U)return 0;
    u16 port=(u16)(bar&~3U);
    u32 control=wx_pci_config(function->bus,function->slot,function->function,4);
    wx_virtio_blk.initial_pci_command=control&65535U;
    /* PCI status is write-one-to-clear; never echo its bits into a config write. */
    /* PCI I/O enabled, but no bus mastering before the IOMMU is ready. */
    wx_pci_write(function->bus,function->slot,function->function,4,
                 ((control&65535U)|1U)&~4U);
    outb(port+0x12,0);
    outb(port+0x12,1);
    outb(port+0x12,3);
    wx_out32(port+4,0); /* no optional features */
    wx_out16(port+0x0e,0);
    u32 probe_stage=1;
    u16 size=wx_in16(port+0x0c);
    u32 used_offset=((16U*size+6U+2U*size+4095U)&~4095U);
    if(size<3 || size>WX_VIRTQ_MAX ||
       used_offset+6U+8U*size>WX_VIRTQ_BYTES)goto fail;
    probe_stage=2;
    wx_vq_memory=(u8*)wx_dma_alloc(function,WX_VIRTQ_BYTES,4096,
                                    WX_DMA_TO_DEVICE|WX_DMA_FROM_DEVICE);
    wx_request=(wx_blk_req*)wx_dma_alloc(function,sizeof(wx_blk_req),16,WX_DMA_TO_DEVICE);
    wx_sector_buffer=(u8*)wx_dma_alloc(function,WX_BLOCK_SECTOR_BYTES,16,WX_DMA_FROM_DEVICE);
    wx_request_status=(volatile u8*)wx_dma_alloc(function,1,16,WX_DMA_FROM_DEVICE);
    if(!wx_vq_memory || !wx_request || !wx_sector_buffer ||
       !wx_request_status)goto fail;
    probe_stage=3;
    wx_virtio_blk.dma_generation=wx_dma_generation;
#ifdef WINUX_INTEL_VTD_PREVIEW
    if(!wx_vtd_attach(function))goto fail;
#endif
    /* Enable bus mastering only AFTER translation is ready. */
    wx_pci_write(function->bus,function->slot,function->function,4,(control&65535U)|5U);
    u64 queue_bus=0;
    if(!wx_dma_bus_address(function,wx_vq_memory,WX_VIRTQ_BYTES,
                           WX_DMA_TO_DEVICE|WX_DMA_FROM_DEVICE,wx_virtio_blk.dma_generation,&queue_bus) ||
       (queue_bus&4095ULL))goto fail;
    probe_stage=4;
    pg_zero(wx_vq_memory,WX_VIRTQ_BYTES);
    wx_out32(port+0x08,(u32)(queue_bus>>12));
    u64 sectors=(u64)wx_in32(port+0x14)|((u64)wx_in32(port+0x18)<<32);
    if(sectors<32 || sectors>0x100000000ULL)goto fail;
    probe_stage=5;
    wx_virtio_blk.port=port;
    wx_virtio_blk.queue_size=size;
    wx_virtio_blk.avail_index=0;
    wx_virtio_blk.used_index=0;
    wx_virtio_blk.initialized=1;
    wx_virtio_owner=function;
    wx_block_device device={"virtio-blk-legacy",sectors,WX_BLOCK_SECTOR_BYTES,
                            WX_BLOCK_FLAG_READ_ONLY,function,wx_virtio_read};
    if(!wx_block_register(&device))goto fail;
    probe_stage=6;
    outb(port+0x12,7);
    return 1;
fail:
    print("VIRTIO_PROBE_REJECT stage=");number(probe_stage);
    print(" queue=");number(size);
    print(" command=");print_hex(wx_pci_config(function->bus,function->slot,function->function,4));
    print(" dma_regions=");number(wx_dma_used);print("\n");
    outb(port+0x12,0); /* reset VirtIO before returning DMA storage */
    /* Even if firmware had bus mastering set on entry, disable it during
     * DMA teardown. Restore the original PCI command only afterwards. */
    wx_pci_write(function->bus,function->slot,function->function,4,
                 (control&65535U)&~4U);
    wx_io_barrier();
#ifdef WINUX_INTEL_VTD_PREVIEW
    if(wx_dma_locked && !wx_vtd_detach(function))return 0;
#endif
    if(!wx_dma_unregister_device(function))return 0;
    wx_virtio_blk.dma_generation=0;
    /* Do not re-enable bus mastering on a detached/unowned controller,
     * even when firmware left COMMAND.BUS_MASTER set previously. */
    wx_pci_write(function->bus,function->slot,function->function,4,
                 (control&65535U)&~4U);
    wx_virtio_owner=0;
    wx_virtio_blk.initialized=0;
    wx_vq_memory=0;wx_request=0;wx_sector_buffer=0;wx_request_status=0;
    return 0;
}
static int wx_virtio_blk_detect(void){
    if(wx_virtio_blk.initialized)return 1;
    static const wx_pci_driver driver={"virtio-blk-legacy",0x1af4U,0x1001U,wx_virtio_probe};
    return wx_pci_bind(&driver);
}
/* Driver lifecycle is separate from the global PCI enumerator. All DMA
 * descriptors complete synchronously in this preview before a detach. */
static int wx_virtio_blk_shutdown(void){
    if(!wx_virtio_blk.initialized||!wx_virtio_owner)return 0;
    wx_pci_function *owner=wx_virtio_owner;
    outb(wx_virtio_blk.port+0x12,0);
    wx_io_barrier();
    int removed=0;
    for(u32 i=0;i<wx_block_count;i++)
        if(wx_block_devices[i].backend==owner){
#ifdef WINUX_VFS_PREVIEW
            /* Invalidate mounted handles BEFORE removing the backing slot. */
            wx_vfs_unmount_device(&wx_block_devices[i]);
#endif
            removed=wx_block_unregister(&wx_block_devices[i]);
            break;
        }
    /* Stop device DMA at the PCI command register BEFORE scrubbing or
     * reusing coherent descriptors. This controller has synchronous I/O. */
    u32 command=wx_pci_config(owner->bus,owner->slot,owner->function,4);
    wx_pci_write(owner->bus,owner->slot,owner->function,4,
                 (command&65535U)&~4U);
    wx_io_barrier();
#ifdef WINUX_INTEL_VTD_PREVIEW
    if(!wx_vtd_detach(owner))return 0;
#endif
    if(!wx_dma_unregister_device(owner))return 0; /* never reuse active DMA */
    wx_virtio_blk.dma_generation=0;
    wx_vq_memory=0;wx_request=0;wx_sector_buffer=0;wx_request_status=0;
    /* Fail-closed: PCI bus mastering stays off after device teardown. */
    wx_pci_write(owner->bus,owner->slot,owner->function,4,
                 wx_virtio_blk.initial_pci_command&~4U);
    wx_pci_release(owner);
    wx_virtio_owner=0;
    wx_virtio_blk.initialized=0;
    wx_virtio_blk.queue_size=0;
    return removed;
}
static int wx_block_device_test(void){
    if(!wx_virtio_blk_detect()){print("BLOCK_TEST_FAILED: no legacy virtio PCI storage\\n");return 0;}
    wx_block_device *device=&wx_block_devices[0];
    /* Unknown drivers must not bind, and DMA of unregistered kernel memory
     * must be rejected by the API. Real IOMMU isolation is still absent. */
    u64 bus_address=0;
    static const wx_pci_driver unknown={"unmatched-native-driver",0xffffU,0xffffU,wx_virtio_probe};
    if(!wx_virtio_owner || !wx_virtio_owner->claimed || !wx_virtio_owner->initialized ||
       device->backend!=wx_virtio_owner ||
       wx_pci_bind(&unknown) ||
       wx_dma_bus_address(wx_virtio_owner,(const void*)&wx_virtio_owner,sizeof(wx_virtio_owner),
                          WX_DMA_FROM_DEVICE,wx_virtio_blk.dma_generation,&bus_address) ||
       wx_dma_bus_address(wx_virtio_owner,wx_sector_buffer,WX_BLOCK_SECTOR_BYTES+1U,
                          WX_DMA_FROM_DEVICE,wx_virtio_blk.dma_generation,&bus_address) ||
       wx_dma_bus_address(wx_virtio_owner,wx_request,sizeof(wx_blk_req),
                          WX_DMA_FROM_DEVICE,wx_virtio_blk.dma_generation,&bus_address) ||
       wx_dma_register(wx_virtio_owner,wx_sys_stack,4096,WX_DMA_TO_DEVICE) ||
       wx_dma_register(wx_virtio_owner,pg_root,4096,WX_DMA_TO_DEVICE))
        return 0;
    /* A live bus master must not be allowed to release DMA reservations. */
    u32 kept_regions=wx_dma_used;
    u64 held_bus=0;
    if(kept_regions!=4 ||
       !(wx_pci_config(wx_virtio_owner->bus,wx_virtio_owner->slot,
                       wx_virtio_owner->function,4)&4U) ||
       wx_dma_unregister_device(wx_virtio_owner) ||
       wx_dma_used!=kept_regions ||
       !wx_dma_bus_address(wx_virtio_owner,wx_request,sizeof(wx_blk_req),
                           WX_DMA_TO_DEVICE,wx_virtio_blk.dma_generation,&held_bus) ||
       held_bus!=(u64)wx_request)return 0;
    print("DMA_MASTER_GUARD_OK: active bus master cannot revoke or scrub its DMA pool\\n");
    print("PNP_DMA_GUARD_OK: PCI exact match, one claim, checked coherent DMA buffers\\n");
    print("DMA_APERTURE_OK: dedicated 32KiB coherent pool rejects kernel stack, page tables, wrong direction and overflow\\n");

    u8 bootsector[512],marker[512],again[512];
    if(!wx_block_read(device,0,1,bootsector) || !wx_block_read(device,16,1,marker) ||
       !wx_block_read(device,16,1,again)){
        print("BLOCK_TEST_FAILED: DMA/queue read\\n");return 0;
    }
    static const char tag[]="WINUX-BLOCK-IO-V1";
    for(unsigned i=0;i<sizeof(tag)-1;i++)if(marker[i]!=tag[i] || again[i]!=tag[i])return 0;
    if(bootsector[510]!=0x55 || bootsector[511]!=0xaa)return 0;
    if(wx_block_read(device,device->capacity_sectors,1,marker))return 0;
    print("BLOCK_READ_OK: post-UEFI PCI legacy VirtIO, DMA sector0/16 twice, bounds and readonly\\n");
    print("BLOCK_DEVICE: driver=");print(device->driver);
    print(" sectors=");number(device->capacity_sectors);print(" sector_size=512\\n");
    /* A complete per-device probe/read/remove/reprobe cycle. The PCI
     * snapshot persists, while driver claim, DMA buffers and block slot
     * must all return to their original empty state. */
    wx_pci_function *previous_owner=wx_virtio_owner;
    u8 *previous_sector=wx_sector_buffer;
    u32 previous_generation=wx_virtio_blk.dma_generation;
    if(!wx_virtio_blk_shutdown() || wx_block_count || wx_dma_used ||
       wx_dma_cursor || wx_virtio_owner || wx_block_devices[0].read ||
       wx_block_read(&wx_block_devices[0],0,1,marker) ||
       wx_dma_bus_address(previous_owner,previous_sector,WX_BLOCK_SECTOR_BYTES,
                          WX_DMA_FROM_DEVICE,previous_generation,&bus_address) ||
       (wx_pci_config(previous_owner->bus,previous_owner->slot,
                      previous_owner->function,4)&4U))return 0;
    for(u32 i=0;i<WX_BLOCK_SECTOR_BYTES;i++)
        if(previous_sector[i])return 0;
    if(!wx_virtio_blk_detect() || wx_block_count!=1 || wx_dma_used!=4 ||
       !wx_block_read(&wx_block_devices[0],16,1,marker) ||
       wx_virtio_blk.dma_generation==previous_generation ||
       wx_dma_bus_address(wx_virtio_owner,previous_sector,WX_BLOCK_SECTOR_BYTES,
                          WX_DMA_FROM_DEVICE,previous_generation,&bus_address))return 0;
    for(unsigned i=0;i<sizeof(tag)-1;i++)if(marker[i]!=tag[i])return 0;
    if(!wx_virtio_blk_shutdown() || wx_block_count || wx_dma_used ||
       wx_virtio_owner || wx_block_devices[0].read)return 0;
    print("PNP_DEVICE_LIFECYCLE_OK: detach/reprobe, DMA unmap, BAR command restored, no orphan block handles\\n");
    print("DMA_REVOKE_OK: stale bus addresses invalidated, coherent pool cleared and reusable\\n");
    print("DMA_GENERATION_OK: stale registration token denied after same PCI device rebound at reused address\\n");
    print("DMA_BUSMASTER_OFF_OK: detached PCI function retains bus mastering disabled after revocation\\n");
    return 1;
}
#ifdef WINUX_INTEL_VTD_PREVIEW
/* Deliberately attempt DMA to an IOVA outside the only mapped 32KiB
 * aperture. This command is for an expendable, read-only QEMU fixture only.
 * The VirtIO controller is reset and unbound immediately after the fault. */
static int wx_vtd_negative_test(void){
    if(!wx_vtd_present || !wx_virtio_blk_detect() || !wx_vtd_enabled)return 0;
    u8 sector[WX_BLOCK_SECTOR_BYTES];
    if(!wx_block_read(&wx_block_devices[0],16,1,sector)){
        print("VTD_FAULT_FAILED: baseline block read\n");wx_virtio_blk_shutdown();return 0;
    }
    const u64 unauthorized=0x08000000ULL;
    print("VTD_HW_DIAG gsts=");print_hex(wx_vtd_read32(0x1c));
    print(" rtaddr=");print_hex(wx_vtd_read64(0x20));
    print(" cap=");print_hex(wx_vtd_cap);
    print(" root0=");print_hex(wx_vtd_root[0]);
    print(" context10=");print_hex(wx_vtd_context[0][0x10*2]);
    print(" pt0=");print_hex(wx_vtd_domains[0].pt[0][0]);
    print("\n");
    wx_virtio_illegal_dma_iova=unauthorized;
    int unexpected=wx_block_read(&wx_block_devices[0],16,1,sector);
    wx_virtio_illegal_dma_iova=0;
    u32 raw=wx_vtd_read32(0x34);
    int caught=wx_vtd_fault_snapshot();
    print("VTD_FAULT_DIAG: transferred=");number(unexpected);
    print(" fsts=");print_hex(raw);
    print(" addr=");print_hex(wx_vtd_last_fault_addr);
    print(" hi=");print_hex(wx_vtd_last_fault_hi);print("\n");
    int removed=wx_virtio_blk_shutdown();
    int denied=!unexpected && caught &&
         (wx_vtd_last_fault_addr&~4095ULL)==unauthorized && removed;
    if(denied)print("VTD_HW_FAULT_OK: unauthorized device DMA blocked and fault recorded\n");
    else print("VTD_HW_FAULT_FAILED: missing hardware evidence\n");
    return denied;
}
#endif
#endif
