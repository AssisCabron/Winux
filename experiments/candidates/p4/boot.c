/* Original experimental x86-64 boot/kernel prototype. No Windows code. */
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef u64 status;
#define EFIERR(n) (0x8000000000000000ULL | (n))
typedef struct { u32 a; u16 b,c; u8 d[8]; } guid;
typedef struct { u64 signature; u32 revision,size,crc,reserved; } header;
typedef struct { header h; void *fn[44]; } boot_services;
typedef struct { guid id; void *table; } config;
typedef struct {
 header h; u16 *vendor; u32 revision; void *in_handle,*in;
 void *out_handle,*out,*err_handle,*err,*runtime; boot_services *boot;
 u64 table_count; config *tables;
} system_table;
typedef struct { u32 version,width,height,format,masks[4],stride; } mode_info;
typedef struct { u32 max_mode,mode; mode_info *info; u64 info_size,base,size; } gop_mode;
typedef struct { void *query,*set,*blt; gop_mode *mode; } gop;
typedef struct {
 u32 revision; void *parent; system_table *system; void *device,*path,*reserved;
 u32 options_size; void *options,*image_base; u64 image_size;
 u32 code_type,data_type; void *unload;
} loaded_image;
/* Versioned, original contract. All pointers are firmware identity-mapped addresses.
 * Image, stack, context and map are retained; no memory is reclaimed yet. */
typedef struct {
 u64 magic,version; void *memory_map; u64 map_size,descriptor_size;
 u32 descriptor_version,reserved;
 u64 framebuffer,framebuffer_size; u32 width,height,stride,pixel_format;
 void *rsdp,*image_base; u64 image_size;
 void *app_arena; u64 app_arena_size;
} boot_context;
static boot_context context;
static u8 map_buffer[65536] __attribute__((aligned(16)));
u8 kernel_stack[65536] __attribute__((aligned(16)));
u8 app_stack[65536] __attribute__((aligned(16)));
u64 app_return_sp;
static inline void outb(u16 p,u8 v) { __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p)); }
static inline u8 inb(u16 p) { u8 v; __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p)); return v; }
static void serial_init(void) {
 outb(0x3f9,0); outb(0x3fb,0x80); outb(0x3f8,1); outb(0x3f9,0);
 outb(0x3fb,3); outb(0x3fa,0xc7); outb(0x3fc,0x0b);
}
static void serial(const char *s) {
 for (;*s;s++) { unsigned spin=100000; while (!(inb(0x3fd)&32) && --spin) {} if(spin) outb(0x3f8,(u8)*s); }
}
static void hex(u64 n) { char b[19]="0x0000000000000000"; for(int i=17;i>=2;i--){ b[i]="0123456789abcdef"[n&15]; n>>=4;} serial(b); serial("\r\n"); }
static int equal_guid(guid *a,guid *b) { u8 *x=(u8*)a,*y=(u8*)b; for(int i=0;i<16;i++) if(x[i]!=y[i]) return 0; return 1; }
#ifdef WINUX_STORAGE_PREVIEW
#include "../../../src/storage/uefi_install_preview.h"
#endif
#include "../../../src/console/console.h"
#include "../../../src/arch/x86_64/hardware.h"
#include "../../../src/arch/x86_64/paging.h"
#include "shell.h"
__attribute__((noreturn)) void kernel_main(boot_context *ctx) {
 __asm__ volatile("cli");
 serial("KERNEL: AFTER_EXIT_BOOT_SERVICES\r\n");
 console_init(ctx);
 serial("KERNEL: own stack active\r\n");
 hardware_init();
 serial("KERNEL: IDT and PIT ready\r\n");
 if(!paging_init()){print("PAGING_INIT_FAILED: unsupported memory layout or CPU\n");for(;;)__asm__ volatile("cli; hlt");}
 serial("KERNEL: OWN_CR3_ACTIVE WP NX\r\n");
 keyboard_init();
 serial(pool_test()?"[OK] Reserved page allocator self-test\r\n":"[FAIL] Page allocator self-test\r\n");
 print("Winux [version 0.38-P4-test]\n\n");
 serial("KERNEL: BOOT_OK\r\n");
 shell();
 __builtin_unreachable();
}
/* MS x64 ABI: RCX is preserved as boot_context; synthesize entry stack alignment. */
__attribute__((naked,noreturn)) static void enter_kernel(boot_context *ctx) {
 __asm__ volatile("cli\n\tleaq kernel_stack+65536(%rip), %rsp\n\tandq $-16, %rsp\n\tsubq $40, %rsp\n\txorq %rbp, %rbp\n\tjmp kernel_main");
}
status EfiMain(void *image, system_table *st) {
 serial_init(); serial("LOADER: original experimental kernel v0.38-P4-test\r\n");
 boot_services *bs=st->boot;
 typedef status (*locate_t)(guid*,void*,void**);
 typedef status (*handle_t)(void*,guid*,void**);
 typedef status (*map_t)(u64*,void*,u64*,u64*,u32*);
 typedef status (*exit_t)(void*,u64);
 typedef status (*watchdog_t)(u64,u64,u64,u16*);
 ((watchdog_t)bs->fn[29])(0,0,0,0);
#ifdef WINUX_STORAGE_PREVIEW
 wx_storage_install_uefi(bs);
#endif
 guid gg={0x9042a9de,0x23dc,0x4a38,{0x96,0xfb,0x7a,0xde,0xd0,0x80,0x51,0x6a}};
 guid ig={0x5b1b31a1,0x9562,0x11d2,{0x8e,0x3f,0,0xa0,0xc9,0x69,0x72,0x3b}};
 guid acpi={0x8868e871,0xe4f1,0x11d3,{0xbc,0x22,0,0x80,0xc7,0x3c,0x88,0x81}};
 context.magic=0x31585443544f4f42ULL; context.version=2;
 typedef status (*pages_t)(u32,u32,u64,u64*);
 u64 app_memory=0;
 if(!((pages_t)bs->fn[2])(0,1,4096,&app_memory)){
  context.app_arena=(void*)app_memory;context.app_arena_size=16*1024*1024;
 }
 loaded_image *li=0;
 if(!((handle_t)bs->fn[16])(image,&ig,(void**)&li)) { context.image_base=li->image_base; context.image_size=li->image_size; }
 gop *graphics=0;
 if(!((locate_t)bs->fn[37])(&gg,0,(void**)&graphics) && graphics->mode && graphics->mode->info) {
  gop_mode *m=graphics->mode; context.framebuffer=m->base; context.framebuffer_size=m->size;
  context.width=m->info->width; context.height=m->info->height;
  context.stride=m->info->stride; context.pixel_format=m->info->format;
 }
 for(u64 i=0;i<st->table_count;i++) if(equal_guid(&st->tables[i].id,&acpi)) context.rsdp=st->tables[i].table;
 context.memory_map=map_buffer;
 for(unsigned attempt=0;attempt<3;attempt++) {
  u64 size=sizeof(map_buffer),key=0,stride=0; u32 version=0;
  status r=((map_t)bs->fn[4])(&size,map_buffer,&key,&stride,&version);
  if(r) {serial("LOADER: GetMemoryMap failed ");hex(r);for(;;) __asm__ volatile("cli; hlt");}
  context.map_size=size;context.descriptor_size=stride;context.descriptor_version=version;
  r=((exit_t)bs->fn[26])(image,key);
  if(!r) enter_kernel(&context);
  if(r!=EFIERR(2)) {serial("LOADER: ExitBootServices failed ");hex(r); for(;;) __asm__ volatile("cli; hlt");}
 }
 serial("LOADER: ExitBootServices retry exhausted\r\n");
 for(;;) __asm__ volatile("cli; hlt");
}

