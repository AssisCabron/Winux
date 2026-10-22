/* QEMU VirtIO 1.0 PCI block transport with mandatory ACCESS_PLATFORM.
 * Only split queue 0, synchronous read-only 512-byte blocks, no MSI-X.
 * Requires Intel VT-d and a scoped PCI 1af4:1042 device.
 * Never load legacy VirtIO when hardware isolation is required.
 */
#ifndef WINUX_VIRTIO_BLK_MODERN_H
#define WINUX_VIRTIO_BLK_MODERN_H
#define WX_MODERN_MAXQ 256U
#define WX_MODERN_VQ_BYTES 16384U
typedef struct __attribute__((packed)) {
    u64 address;u32 length;u16 flags,next;
} wx_mod_desc;
typedef struct __attribute__((packed)){u32 type,reserved;u64 sector;} wx_mod_req;
static struct {
    wx_pci_function *owner;
    volatile u8 *common,*notify,*cfg;
    u32 multiplier;
    u16 n,notify_offset,index;
    u16 initial_command;
    int initialized;
    u8 *ring,*sector;
    wx_mod_req *req;
    volatile u8 *status;
} wx_mod;
static u64 wx_mod_fault_iova;
static u8 wx_mod_pci8(const wx_pci_function *p,u8 off){
    return (u8)(wx_pci_config(p->bus,p->slot,p->function,off)>>((off&3U)*8));
}
static u32 wx_mod_mm32(volatile u8 *ptr){return *(volatile u32*)ptr;}
static u64 wx_mod_mm64(volatile u8 *ptr){return *(volatile u64*)ptr;}
static u16 wx_mod_mm16(volatile u8 *ptr){return *(volatile u16*)ptr;}
static void wx_mod_wr32(volatile u8 *ptr,u32 v){*(volatile u32*)ptr=v;__asm__ volatile("":::"memory");}
static void wx_mod_wr64(volatile u8 *ptr,u64 v){*(volatile u64*)ptr=v;__asm__ volatile("":::"memory");}
static void wx_mod_wr16(volatile u8 *ptr,u16 v){*(volatile u16*)ptr=v;__asm__ volatile("":::"memory");}
static void wx_mod_wr8(volatile u8 *ptr,u8 v){*ptr=v;__asm__ volatile("":::"memory");}
/* Firmware assigns the modern VirtIO 64-bit BAR above 4 GiB in Q35.
 * Map its 16KiB MMIO aperture into a reserved supervisor-only VA window.
 * Paging's normal low-RAM allocator is intentionally NOT used here. */
#define WX_MOD_MMIO_VA (PG_WINDOW+PG_WINDOW_SIZE-0x200000ULL)
static u64 wx_mod_mmio_bar_pa;
static int wx_mod_mmio_ready;
static volatile u8 *wx_mod_cap(const wx_pci_function *p,u8 bar,u32 offset,u32 bytes){
    if(bar>=6||!bytes||bytes>0x1000||offset>0x4000||
       offset+bytes>0x4000)return 0;
    u32 value=p->bars[bar];
    if(value&1U)return 0;
    u64 base=(u64)(value&~15U);
    if((value&6U)==4U){
        if(bar>=5)return 0;
        base|=(u64)p->bars[bar+1]<<32;
    }
    if(!base || base>PG_MASK-0x4000ULL || (base&4095ULL))return 0;
    if(base<0x100000000ULL)return (volatile u8*)(base+offset);
    if(bar!=4)return 0; /* bounded fixed BAR4 64-bit scope */
    if(wx_mod_mmio_ready && wx_mod_mmio_bar_pa!=base)return 0;
    if(!wx_mod_mmio_ready){
        for(u64 i=0;i<0x4000;i+=4096){
            u64 va=WX_MOD_MMIO_VA+i;
            u64 *e=pg_leaf(va,1);
            if(!e||(*e&PG_PRESENT))return 0;
            *e=((base+i)&PG_MASK)|PG_PRESENT|PG_WRITE|PG_NX|0x18ULL;
            pg_flush(va);
        }
        wx_mod_mmio_bar_pa=base;
        wx_mod_mmio_ready=1;
    }
    return (volatile u8*)(WX_MOD_MMIO_VA+offset);
}
static int wx_mod_caps(wx_pci_function *p){
    u8 at=wx_mod_pci8(p,0x34),visited=0;
    print("VTD_MOD_CAPS first=");print_hex(at);
    for(u32 j=0;j<6;j++){print(" bar");number(j);print("=");print_hex(p->bars[j]);}
    print("\n");
    while(at>=0x40 && at<0x100-16 && visited++<48){
        if((at&3U))return 0;
        u8 id=wx_mod_pci8(p,at),next=wx_mod_pci8(p,at+1);
        if(id==9 && wx_mod_pci8(p,at+2)>=16){
            u8 type=wx_mod_pci8(p,at+3),bar=wx_mod_pci8(p,at+4);
            u32 off=wx_pci_config(p->bus,p->slot,p->function,at+8);
            u32 len=wx_pci_config(p->bus,p->slot,p->function,at+12);
            volatile u8 *addr=wx_mod_cap(p,bar,off,len);
            print("VTD_MOD_CAP id=");number(id);print(" type=");number(type);
            print(" bar=");number(bar);print(" off=");print_hex(off);
            print(" len=");number(len);print(" addr=");print_hex((u64)addr);print("\n");
            if(type==1 && len>=56)wx_mod.common=addr;
            if(type==2 && len>=2 && wx_mod_pci8(p,at+2)>=20){
                wx_mod.notify=addr;
                wx_mod.multiplier=wx_pci_config(p->bus,p->slot,p->function,at+16);
            }
            if(type==4 && len>=8)wx_mod.cfg=addr;
        }
        if(!next)break;
        at=next;
    }
    return wx_mod.common && wx_mod.notify && wx_mod.cfg && wx_mod.multiplier;
}
static int wx_mod_read_one(wx_block_device *device,u64 lba,void *dest){
    if(!wx_mod.initialized||!device||!dest||lba>=device->capacity_sectors)return 0;
    u16 n=wx_mod.n;
    u32 usedoff=(16U*n+6U+2U*n+4095U)&~4095U;
    volatile wx_mod_desc *d=(volatile wx_mod_desc*)wx_mod.ring;
    volatile u16 *av=(volatile u16*)(wx_mod.ring+16*n);
    volatile u16 *used=(volatile u16*)(wx_mod.ring+usedoff);
    u64 hb=0,sb=0,stb=0;
    if(!wx_dma_bus_address(wx_mod.owner,wx_mod.req,sizeof(wx_mod_req),WX_DMA_TO_DEVICE,wx_dma_generation,&hb)||
       !wx_dma_bus_address(wx_mod.owner,wx_mod.sector,512,WX_DMA_FROM_DEVICE,wx_dma_generation,&sb)||
       !wx_dma_bus_address(wx_mod.owner,(const void*)wx_mod.status,1,WX_DMA_FROM_DEVICE,wx_dma_generation,&stb))return 0;
    *wx_mod.req=(wx_mod_req){0,0,lba};
    *wx_mod.status=0xff;
    if(wx_mod_fault_iova)sb=wx_mod_fault_iova;
    d[0]=(wx_mod_desc){hb,16,1,1};
    d[1]=(wx_mod_desc){sb,512,3,2};
    d[2]=(wx_mod_desc){stb,1,2,0};
    av[2+(wx_mod.index%n)]=0;
    __asm__ volatile("":::"memory");
    wx_mod.index++;av[1]=wx_mod.index;
    __asm__ volatile("":::"memory");
    wx_mod_wr16(wx_mod.notify+wx_mod.notify_offset*wx_mod.multiplier,0);
    u32 tries=0;
    while(used[1]!=wx_mod.index&&tries++<10000000U)__asm__ volatile("pause");
    /* QEMU may complete a VirtIO request with status=OK even when the
     * IOMMU rejected a descriptor. Hardware fault evidence overrides it. */
    if(wx_vtd_fault_snapshot()){
        print("VTD_IO_FAULT_REJECTED: DMA completion ignored after remapping fault\n");
        return 0;
    }
    if(used[1]!=wx_mod.index||*wx_mod.status!=0)return 0;
    for(u32 i=0;i<512;i++)((u8*)dest)[i]=wx_mod.sector[i];
    return 1;
}
static int wx_mod_read(wx_block_device *dev,u64 lba,u32 count,void *dest){
    if(!dev||!dest||count>128U)return 0;
    for(u32 i=0;i<count;i++)if(!wx_mod_read_one(dev,lba+i,(u8*)dest+512*i))return 0;
    return 1;
}
static int wx_mod_shutdown(void){
    if(!wx_mod.owner)return 0;
    wx_pci_function *p=wx_mod.owner;
    wx_mod_wr8(wx_mod.common+0x14,0);
    u32 c=wx_pci_config(p->bus,p->slot,p->function,4);
    wx_pci_write(p->bus,p->slot,p->function,4,(c&65535U)&~4U);
    __asm__ volatile("":::"memory");
    for(u32 i=0;i<wx_block_count;i++)if(wx_block_devices[i].backend==p){
#ifdef WINUX_VFS_PREVIEW
        wx_vfs_unmount_device(&wx_block_devices[i]);
#endif
        wx_block_unregister(&wx_block_devices[i]);break;
    }
    if(!wx_vtd_detach(p)||!wx_dma_unregister_device(p))return 0;
    wx_pci_write(p->bus,p->slot,p->function,4,wx_mod.initial_command&~4U);
    wx_pci_release(p);
    pg_zero(&wx_mod,sizeof(wx_mod));
    return 1;
}
static int wx_mod_probe(wx_pci_function *p){
    if(!p||p->vendor!=0x1af4U||p->device!=0x1042U||!wx_vtd_present)return 0;
    wx_mod.owner=p;
    u32 ctrl=wx_pci_config(p->bus,p->slot,p->function,4);
    wx_mod.initial_command=(u16)ctrl;
    wx_pci_write(p->bus,p->slot,p->function,4,((ctrl&65535U)|2U)&~4U);
    if(!wx_mod_caps(p)){print("VTD_MODERN_STAGE_FAIL caps\n");goto fail;}
    volatile u8 *c=wx_mod.common;
    wx_mod_wr8(c+0x14,0);
    wx_mod_wr8(c+0x14,3); /* ACKNOWLEDGE|DRIVER */
    wx_mod_wr32(c,1);
    u32 high=wx_mod_mm32(c+4);
    if((high&3U)!=3U){print("VTD_MODERN_STAGE_FAIL access_platform feature\n");goto fail;}
    wx_mod_wr32(c+8,0);wx_mod_wr32(c+12,0);
    wx_mod_wr32(c+8,1);wx_mod_wr32(c+12,3); /* VERSION_1 and ACCESS_PLATFORM */
    wx_mod_wr8(c+0x14,11); /* FEATURES_OK */
    if(!(c[0x14]&8U)){print("VTD_MODERN_STAGE_FAIL features_ok\n");goto fail;}
    wx_mod_wr16(c+0x16,0);
    wx_mod.n=wx_mod_mm16(c+0x18);
    u32 usedoff=(16U*wx_mod.n+6U+2U*wx_mod.n+4095U)&~4095U;
    if(wx_mod.n<3||wx_mod.n>WX_MODERN_MAXQ||
       usedoff+6U+8U*wx_mod.n>WX_MODERN_VQ_BYTES)goto fail;
    wx_mod.ring=(u8*)wx_dma_alloc(p,WX_MODERN_VQ_BYTES,4096,3);
    wx_mod.req=(wx_mod_req*)wx_dma_alloc(p,sizeof(wx_mod_req),16,1);
    wx_mod.sector=(u8*)wx_dma_alloc(p,512,4096,2); /* isolated PTE */
    wx_mod.status=(volatile u8*)wx_dma_alloc(p,1,4096,2); /* separate PTE */
    if(!wx_mod.ring||!wx_mod.req||!wx_mod.sector||!wx_mod.status)goto fail;
    wx_mod.notify_offset=wx_mod_mm16(c+0x1e);
    if((u64)wx_mod.notify_offset*wx_mod.multiplier>0xffeULL)goto fail;
    if(!wx_vtd_attach(p)){print("VTD_MODERN_STAGE_FAIL vtd_attach\n");goto fail;}
    wx_mod_wr64(c+0x20,(u64)wx_mod.ring);
    wx_mod_wr64(c+0x28,(u64)(wx_mod.ring+16*wx_mod.n));
    wx_mod_wr64(c+0x30,(u64)(wx_mod.ring+usedoff));
    wx_mod_wr16(c+0x1c,1);
    wx_pci_write(p->bus,p->slot,p->function,4,(ctrl&65535U)|6U);
    wx_mod_wr8(c+0x14,15); /* DRIVER_OK */
    if(!(c[0x14]&4U))goto fail;
    u64 sectors=wx_mod_mm64(wx_mod.cfg);
    if(sectors<32||sectors>0x100000000ULL)goto fail;
    wx_mod.initialized=1;
    wx_block_device blk={"virtio-blk-modern-vtd",sectors,512,
                          WX_BLOCK_FLAG_READ_ONLY,p,wx_mod_read};
    if(!wx_block_register(&blk))goto fail;
    print("VTD_MODERN_BOUND_OK: PCI VirtIO 1.0 ACCESS_PLATFORM BDF=");
    print_hex(((u64)p->bus<<8)|((u64)p->slot<<3)|p->function);
    print(" queue_size=");number(wx_mod.n);print("\n");
    return 1;
fail:
    if(wx_mod.common)wx_mod_wr8(wx_mod.common+0x14,0);
    wx_pci_write(p->bus,p->slot,p->function,4,((ctrl&65535U)|2U)&~4U);
    if(wx_dma_locked&&!wx_vtd_detach(p))return 0;
    if(!wx_dma_unregister_device(p))return 0;
    wx_pci_write(p->bus,p->slot,p->function,4,ctrl&~4U);
    pg_zero(&wx_mod,sizeof(wx_mod));
    return 0;
}
static int wx_mod_detect(void){
    if(wx_mod.initialized)return 1;
    static const wx_pci_driver drv={"virtio-blk-modern-vtd",0x1af4,0x1042,wx_mod_probe};
    return wx_pci_bind(&drv);
}
static int wx_mod_block_test(void){
    if(!wx_mod_detect())return 0;
    u8 sector[512];
    int ok=wx_block_read(&wx_block_devices[0],16,1,sector);
    static const char tag[]="WINUX-BLOCK-IO-V1";
    for(u32 i=0;ok&&i<sizeof(tag)-1;i++)if(sector[i]!=tag[i])ok=0;
    if(!wx_mod_shutdown())ok=0;
    if(ok && !wx_mod_detect())ok=0;
    if(ok && !wx_block_read(&wx_block_devices[0],16,1,sector))ok=0;
    if(wx_mod.initialized && !wx_mod_shutdown())ok=0;
    if(ok)print("VTD_MODERN_DMA_OK: ACCESS_PLATFORM, DMA through VT-d, detach/reprobe\n");
    else print("VTD_MODERN_DMA_FAILED\n");
    return ok;
}
static int wx_mod_fault_test(void){
    if(!wx_mod_detect())return 0;
    u8 sector[512];
    if(!wx_block_read(&wx_block_devices[0],16,1,sector)){wx_mod_shutdown();return 0;}
    const u64 illegal=0x08000000ULL;
    u32 recorded_before=wx_vtd_faults;
    wx_mod_fault_iova=illegal;
    int transferred=wx_block_read(&wx_block_devices[0],16,1,sector);
    wx_mod_fault_iova=0;
    u32 fsts=wx_vtd_read32(0x34);
    int caught=(wx_vtd_faults>recorded_before)||wx_vtd_fault_snapshot();
    print("VTD_MODERN_FAULT_DIAG transfer=");number(transferred);
    print(" fsts=");print_hex(fsts);
    print(" addr=");print_hex(wx_vtd_last_fault_addr);
    print(" record=");print_hex(wx_vtd_last_fault_hi);print("\n");
    int shutdown=wx_mod_shutdown();
    int ok=!transferred&&caught&&
         (wx_vtd_last_fault_addr&~4095ULL)==illegal&&shutdown;
    print(ok?"VTD_MODERN_HW_FAULT_OK: hardware blocked unauthorized IOVA\n":
             "VTD_MODERN_HW_FAULT_FAILED\n");
    return ok;
}
static int wx_mod_invalidation_test(void){
    if(!wx_mod_detect())return 0;
    u8 sector[512];
    if(!wx_block_read(&wx_block_devices[0],16,1,sector)){
        wx_mod_shutdown();return 0;
    }
    wx_vtd_domain *dom=0;
    u16 bdf=((u16)wx_mod.owner->bus<<8)|((u16)wx_mod.owner->slot<<3)|wx_mod.owner->function;
    for(u32 i=0;i<WX_VTD_DOMAINS;i++)
        if(wx_vtd_domains[i].active&&wx_vtd_domains[i].bdf==bdf)
            dom=&wx_vtd_domains[i];
    if(!dom){wx_mod_shutdown();return 0;}
    u64 a=(u64)wx_mod.sector,first=(u64)wx_dma_pool;
    u64 offset=(a>>21)-(first>>21);
    if(offset>1){wx_mod_shutdown();return 0;}
    u64 *pte=&dom->pt[offset][(a>>12)&511];
    u64 original=*pte;
    if(!(original&2ULL)){wx_mod_shutdown();return 0;} /* sector is device-WRITE only */
    u32 invalidations_before=wx_vtd_invalidations;
    u32 faults_before=wx_vtd_faults;
    /* A translation known to have worked is downgraded READ-only. The next
     * VirtIO DATA-IN needs a device WRITE and must fault AFTER IOTLB flush. */
    *pte=original&~2ULL;
    __asm__ volatile("":::"memory");
    if(!wx_vtd_iotlb_invalidate()){
        *pte=original;wx_mod_shutdown();return 0;
    }
    int unexpected=wx_block_read(&wx_block_devices[0],16,1,sector);
    int caught=(wx_vtd_faults>faults_before)||wx_vtd_fault_snapshot();
    u64 fault=wx_vtd_last_fault_addr;
    *pte=original;
    __asm__ volatile("":::"memory");
    int reflushed=wx_vtd_iotlb_invalidate();
    int removed=wx_mod_shutdown();
    int ok=!unexpected&&caught&&reflushed&&removed&&
        wx_vtd_invalidations>=invalidations_before+2&&
        (fault&~4095ULL)==(a&~4095ULL);
    if(ok){
        int rebound=wx_mod_detect();
        if(rebound){
            ok=wx_block_read(&wx_block_devices[0],16,1,sector);
            if(!wx_mod_shutdown())ok=0;
        }else ok=0;
    }
    print("VTD_IOTLB_DIAG blocked=");number(!unexpected);
    print(" hw_fault=");number(caught);
    print(" fault_addr=");print_hex(fault);
    print(" invalidations=");number(wx_vtd_invalidations);print("\n");
    print(ok?"VTD_IOTLB_INVALIDATION_OK: live permission revoke, IOTLB flushed, fault, remap and reprobe\n":
             "VTD_IOTLB_INVALIDATION_FAILED\n");
    return ok;
}
#endif
