/* Generic PCI function discovery and driver binding for native Winux backends.
 * Legacy config mechanism #1, one CPU; not Windows PnP/INF/ACPI or hotplug.
 * A PCI function is claimed once, by an explicitly matching native driver.
 */
#ifndef WINUX_PCI_PNP_H
#define WINUX_PCI_PNP_H
#define WX_PCI_MAX_FUNCTIONS 64U
typedef struct {
    u16 vendor,device;
    u8 bus,slot,function,class_code,subclass,prog_if,irq;
    u32 bars[6];
    u8 claimed;
    u8 initialized;
} wx_pci_function;
static wx_pci_function wx_pci_devices[WX_PCI_MAX_FUNCTIONS];
static u32 wx_pci_count,wx_pci_overflow;
static int wx_pci_scanned;
static inline void wx_out16(u16 p,u16 v){__asm__ volatile("outw %0,%1"::"a"(v),"Nd"(p));}
static inline u16 wx_in16(u16 p){u16 v;__asm__ volatile("inw %1,%0":"=a"(v):"Nd"(p));return v;}
static inline void wx_out32(u16 p,u32 v){__asm__ volatile("outl %0,%1"::"a"(v),"Nd"(p));}
static inline u32 wx_in32(u16 p){u32 v;__asm__ volatile("inl %1,%0":"=a"(v):"Nd"(p));return v;}
static u32 wx_pci_config(u8 bus,u8 slot,u8 func,u8 off) {
    wx_out32(0xcf8,0x80000000U|((u32)bus<<16)|((u32)slot<<11)|((u32)func<<8)|(off&0xfcU));
    return wx_in32(0xcfc);
}
static void wx_pci_write(u8 bus,u8 slot,u8 func,u8 off,u32 v) {
    wx_out32(0xcf8,0x80000000U|((u32)bus<<16)|((u32)slot<<11)|((u32)func<<8)|(off&0xfcU));
    wx_out32(0xcfc,v);
}
static void wx_pci_enumerate(void) {
    if(wx_pci_scanned)return;
    wx_pci_scanned=1;wx_pci_count=0;wx_pci_overflow=0;
    for(u32 bus=0;bus<256;bus++)
      for(u32 slot=0;slot<32;slot++) {
        u32 parent=wx_pci_config((u8)bus,(u8)slot,0,0);
        if((parent&0xffffU)==0xffffU)continue;
        u32 header=wx_pci_config((u8)bus,(u8)slot,0,0xc);
        u32 count=((header>>23)&1U)?8U:1U;
        for(u32 function=0;function<count;function++) {
            u32 id=wx_pci_config((u8)bus,(u8)slot,(u8)function,0);
            if((id&0xffffU)==0xffffU)continue;
            if(wx_pci_count>=WX_PCI_MAX_FUNCTIONS){wx_pci_overflow++;continue;}
            wx_pci_function *p=&wx_pci_devices[wx_pci_count++];
            *p=(wx_pci_function){0};
            p->vendor=(u16)id;p->device=(u16)(id>>16);
            p->bus=(u8)bus;p->slot=(u8)slot;p->function=(u8)function;
            u32 cls=wx_pci_config(p->bus,p->slot,p->function,8);
            p->class_code=(u8)(cls>>24);p->subclass=(u8)(cls>>16);
            p->prog_if=(u8)(cls>>8);
            p->irq=(u8)wx_pci_config(p->bus,p->slot,p->function,0x3c);
            for(unsigned b=0;b<6;b++)
                p->bars[b]=wx_pci_config(p->bus,p->slot,p->function,(u8)(0x10+b*4));
        }
    }
}
static wx_pci_function *wx_pci_find(u16 vendor,u16 device) {
    wx_pci_enumerate();
    for(u32 i=0;i<wx_pci_count;i++)
        if(wx_pci_devices[i].vendor==vendor&&wx_pci_devices[i].device==device &&
           !wx_pci_devices[i].claimed)return &wx_pci_devices[i];
    return 0;
}
static int wx_pci_claim(wx_pci_function *function){
    if(!function || function->claimed)return 0;
    int known=0;
    for(u32 i=0;i<wx_pci_count;i++)if(function==&wx_pci_devices[i])known=1;
    if(!known)return 0;
    function->claimed=1;return 1;
}
static void wx_pci_release(wx_pci_function *function){
    if(function){function->initialized=0;function->claimed=0;}
}
/* A generic PCI native driver match table. No speculative DeviceObjects,
 * fake WDM callbacks, or binding unsupported vendor/device pairs. */
typedef struct {
    const char *name;
    u16 vendor,device;
    int (*probe)(wx_pci_function *function);
} wx_pci_driver;
static int wx_pci_bind(const wx_pci_driver *driver){
    if(!driver||!driver->name||!driver->probe)return 0;
    wx_pci_function *function=wx_pci_find(driver->vendor,driver->device);
    if(!function || !wx_pci_claim(function))return 0;
    if(!driver->probe(function)){
        wx_pci_release(function);return 0;
    }
    function->initialized=1;
    return 1;
}
#endif
