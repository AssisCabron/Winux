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
} wx_virtio_blk;

static u8 wx_vq_memory[WX_VIRTQ_BYTES] __attribute__((aligned(4096)));
static wx_blk_req wx_request __attribute__((aligned(16)));
static u8 wx_sector_buffer[WX_BLOCK_SECTOR_BYTES] __attribute__((aligned(16)));
static volatile u8 wx_request_status __attribute__((aligned(16)));

static wx_pci_function *wx_virtio_owner;
static inline void wx_io_barrier(void){__asm__ volatile("":::"memory");}

/* One hardware queue: zero-copied descriptor metadata, bounced 512-byte sector. */
static int wx_virtio_read_one(wx_block_device *device,u64 lba,void *destination) {
    if(!wx_virtio_blk.initialized || !device || !destination || lba>=device->capacity_sectors)return 0;
    u16 n=wx_virtio_blk.queue_size,port=wx_virtio_blk.port;
    volatile wx_vq_desc *desc=(volatile wx_vq_desc*)wx_vq_memory;
    volatile u16 *avail=(volatile u16*)(wx_vq_memory+16*n);
    u32 used_offset=((16U*n+6U+2U*n+4095U)&~4095U);
    volatile u16 *used=(volatile u16*)(wx_vq_memory+used_offset);
    if(used_offset+6U+8U*n>sizeof(wx_vq_memory))return 0;
    u64 req_bus=0,sector_bus=0,status_bus=0;
    if(!wx_dma_bus_address(wx_virtio_owner,&wx_request,sizeof(wx_request),WX_DMA_TO_DEVICE,&req_bus) ||
       !wx_dma_bus_address(wx_virtio_owner,wx_sector_buffer,sizeof(wx_sector_buffer),WX_DMA_FROM_DEVICE,&sector_bus) ||
       !wx_dma_bus_address(wx_virtio_owner,(const void*)&wx_request_status,1,WX_DMA_FROM_DEVICE,&status_bus))return 0;
    wx_request=(wx_blk_req){0U,0U,lba}; /* VIRTIO_BLK_T_IN */
    wx_request_status=0xff;
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
    if(used[1]!=wx_virtio_blk.avail_index || wx_request_status!=0)return 0;
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
    if(!function || function->vendor!=0x1af4U || function->device!=0x1001U)return 0;
    u32 bar=function->bars[0];
    if(!(bar&1U) || (bar&~3U)>0xff00U || (bar&~3U)<0x100U)return 0;
    u16 port=(u16)(bar&~3U);
    u32 control=wx_pci_config(function->bus,function->slot,function->function,4);
    wx_pci_write(function->bus,function->slot,function->function,4,control|5U);
    outb(port+0x12,0);
    outb(port+0x12,1);
    outb(port+0x12,3);
    wx_out32(port+4,0); /* no optional features */
    wx_out16(port+0x0e,0);
    u16 size=wx_in16(port+0x0c);
    u32 used_offset=((16U*size+6U+2U*size+4095U)&~4095U);
    if(size<3 || size>WX_VIRTQ_MAX ||
       used_offset+6U+8U*size>sizeof(wx_vq_memory))goto fail;
    if(!wx_dma_register(function,wx_vq_memory,sizeof(wx_vq_memory),
                        WX_DMA_TO_DEVICE|WX_DMA_FROM_DEVICE) ||
       !wx_dma_register(function,&wx_request,sizeof(wx_request),WX_DMA_TO_DEVICE) ||
       !wx_dma_register(function,wx_sector_buffer,sizeof(wx_sector_buffer),WX_DMA_FROM_DEVICE) ||
       !wx_dma_register(function,(const void*)&wx_request_status,1,WX_DMA_FROM_DEVICE))goto fail;
    u64 queue_bus=0;
    if(!wx_dma_bus_address(function,wx_vq_memory,sizeof(wx_vq_memory),
                           WX_DMA_TO_DEVICE|WX_DMA_FROM_DEVICE,&queue_bus) ||
       (queue_bus&4095ULL))goto fail;
    pg_zero(wx_vq_memory,sizeof(wx_vq_memory));
    wx_out32(port+0x08,(u32)(queue_bus>>12));
    u64 sectors=(u64)wx_in32(port+0x14)|((u64)wx_in32(port+0x18)<<32);
    if(sectors<32 || sectors>0x100000000ULL)goto fail;
    wx_virtio_blk.port=port;
    wx_virtio_blk.queue_size=size;
    wx_virtio_blk.avail_index=0;
    wx_virtio_blk.used_index=0;
    wx_virtio_blk.initialized=1;
    wx_virtio_owner=function;
    wx_block_device device={"virtio-blk-legacy",sectors,WX_BLOCK_SECTOR_BYTES,
                            WX_BLOCK_FLAG_READ_ONLY,function,wx_virtio_read};
    if(!wx_block_register(&device))goto fail;
    outb(port+0x12,7);
    return 1;
fail:
    outb(port+0x12,0x83);
    wx_virtio_owner=0;
    wx_virtio_blk.initialized=0;
    wx_dma_unregister_device(function);
    return 0;
}
static int wx_virtio_blk_detect(void){
    if(wx_virtio_blk.initialized)return 1;
    static const wx_pci_driver driver={"virtio-blk-legacy",0x1af4U,0x1001U,wx_virtio_probe};
    return wx_pci_bind(&driver);
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
                          WX_DMA_FROM_DEVICE,&bus_address) ||
       wx_dma_bus_address(wx_virtio_owner,wx_sector_buffer,sizeof(wx_sector_buffer)+1U,
                          WX_DMA_FROM_DEVICE,&bus_address))return 0;
    print("PNP_DMA_GUARD_OK: PCI exact match, one claim, checked coherent DMA buffers\\n");

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
    return 1;
}
#endif
