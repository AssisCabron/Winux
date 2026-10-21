/* One-shot Ring 3 proof: supervisor isolation and bounded int 0x80 gate.
 * Compiled only in the isolated WINUX_RING3_PREVIEW build, never stable 0.38.
 * Not a process scheduler, Linux ABI, or general NT syscall replacement. */
#include "../../../abi/native_services_v1.h"
#define WX_USER_CODE (PG_WINDOW + 0x0f000000ULL)
#define WX_USER_STACK (PG_WINDOW + 0x0f001000ULL)
#define WX_USER_PAGE 4ULL
#define WX_USER_CS 0x23
#define WX_USER_SS 0x1b

typedef struct __attribute__((packed)) {
    u32 reserved0;
    u64 rsp0, rsp1, rsp2, reserved1;
    u64 ist1, ist2, ist3, ist4, ist5, ist6, ist7;
    u64 reserved2;
    u16 reserved3, io_map_base;
} wx_tss64;
_Static_assert(sizeof(wx_tss64)==104, "x86-64 TSS must be 104 bytes");
static wx_tss64 wx_tss;
static u8 wx_sys_stack[16384] __attribute__((aligned(16)));
static volatile u64 wx_saved_rsp __attribute__((used));
static volatile u64 wx_fault_vector,wx_fault_error;
static volatile u32 wx_active,wx_exit_code,wx_write_count,wx_exit_requested;
/* Kernel faults remain fatal; only faults originating in CPL3 are recovered. */

__attribute__((naked,used)) static void wx_ring3_restore(void) {
    __asm__ volatile(
      "movq wx_saved_rsp(%rip),%rsp;"
      "movw $16,%ax; movw %ax,%ds; movw %ax,%es; movw %ax,%fs; movw %ax,%gs;"
      "movl wx_exit_code(%rip),%eax;"
      "popq %r15; popq %r14; popq %r13; popq %r12;"
      "popq %rsi; popq %rdi; popq %rbx; popq %rbp; ret");
}
__attribute__((naked)) static void wx_user_gp(void) {
    __asm__ volatile(
      "cmpl $1,wx_active(%rip); jne 1f;"
      "movq 16(%rsp),%rax; andl $3,%eax; cmpl $3,%eax; jne 1f;"
      "movq $13,wx_fault_vector(%rip);"
      "movq (%rsp),%rax; movq %rax,wx_fault_error(%rip);"
      "movl $97,wx_exit_code(%rip); jmp wx_ring3_restore;"
      "1: movq $13,%rcx; movq (%rsp),%rdx; subq $40,%rsp; call kernel_fault; ud2");
}
__attribute__((naked)) static void wx_user_pf(void) {
    __asm__ volatile(
      "cmpl $1,wx_active(%rip); jne 1f;"
      "movq 16(%rsp),%rax; andl $3,%eax; cmpl $3,%eax; jne 1f;"
      "movq $14,wx_fault_vector(%rip);"
      "movq (%rsp),%rax; movq %rax,wx_fault_error(%rip);"
      "movl $98,wx_exit_code(%rip); jmp wx_ring3_restore;"
      "1: movq $14,%rcx; movq (%rsp),%rdx; subq $40,%rsp; call kernel_fault; ud2");
}
static int wx_user_readable(u64 addr,u64 size) {
    if (!size || size>WX_NATIVE_MAX_WRITE || addr<WX_USER_CODE || addr>WX_USER_CODE+4096-size)return 0;
    for (u64 va=addr&~4095ULL;va<addr+size;va+=4096) {
        u64 *leaf=pg_leaf(va,0);
        if (!leaf || !(*leaf & (PG_PRESENT|4ULL)) || ((*leaf&5ULL)!=5ULL))return 0;
    }
    return 1;
}
/* Frame saved in order rax, rcx, rdx, rbx, rbp, rsi, rdi, r8 ... r15. */
__attribute__((used,noinline)) static void wx_sys_dispatch(u64 *frame) {
    u64 number=frame[0],arg0=frame[6],arg1=frame[5],arg2=frame[2];
    if (number==WX_NATIVE_EXIT) {
        wx_exit_code=(u32)arg0;
        wx_exit_requested=1;
        frame[0]=0;
        return;
    }
    if (number==WX_NATIVE_CONSOLE_WRITE && arg0==WX_NATIVE_STDOUT_HANDLE && wx_user_readable(arg1,arg2)) {
        for (u64 i=0;i<arg2;i++)console_char(((const char *)arg1)[i]);
        wx_write_count+=(u32)arg2;
        frame[0]=arg2;
        return;
    }
    frame[0]=~37ULL; /* -ENOSYS / invalid request for this preview. */
}
__attribute__((naked)) static void wx_sys_gate(void) {
    __asm__ volatile(
      "pushq %r15; pushq %r14; pushq %r13; pushq %r12; pushq %r11;"
      "pushq %r10; pushq %r9; pushq %r8; pushq %rdi; pushq %rsi;"
      "pushq %rbp; pushq %rbx; pushq %rdx; pushq %rcx; pushq %rax;"
      "movq %rsp,%rcx; subq $32,%rsp; call wx_sys_dispatch; addq $32,%rsp;"
      "cmpl $1,wx_exit_requested(%rip); je wx_ring3_restore;"
      "popq %rax; popq %rcx; popq %rdx; popq %rbx; popq %rbp;"
      "popq %rsi; popq %rdi; popq %r8; popq %r9; popq %r10;"
      "popq %r11; popq %r12; popq %r13; popq %r14; popq %r15; iretq");
}
__attribute__((naked,noinline)) static u32 wx_enter_user(u64 start,u64 stack_top) {
    __asm__ volatile(
      "pushq %rbp; pushq %rbx; pushq %rdi; pushq %rsi;"
      "pushq %r12; pushq %r13; pushq %r14; pushq %r15;"
      "movq %rsp,wx_saved_rsp(%rip);"
      "movw $0x1b,%ax; movw %ax,%ds; movw %ax,%es; movw %ax,%fs; movw %ax,%gs;"
      "pushq $0x1b; pushq %rdx; pushfq;"
      "orq $0x200,(%rsp); andq $-1025,(%rsp);"
      "pushq $0x23; pushq %rcx; iretq");
}
static int wx_prepare_tss(void) {
    pg_zero(&wx_tss,sizeof(wx_tss));
    wx_tss.rsp0=((u64)wx_sys_stack+sizeof(wx_sys_stack)) & ~15ULL;
    wx_tss.io_map_base=sizeof(wx_tss);
    u64 base=(u64)&wx_tss,limit=sizeof(wx_tss)-1;
    kernel_gdt[3]=0x00cff2000000ffffULL; /* SS user DPL3. */
    kernel_gdt[4]=0x00affa000000ffffULL; /* CS user 64-bit DPL3. */
    kernel_gdt[5]=(limit&65535ULL)|((base&0xffffffULL)<<16)|(0x89ULL<<40)|
        (((limit>>16)&15ULL)<<48)|(((base>>24)&255ULL)<<56);
    kernel_gdt[6]=base>>32;
    /* GDT was already loaded by hardware_init; refresh after adding TSS. */
    idt_pointer gdtr={sizeof(kernel_gdt)-1,(u64)kernel_gdt};
    __asm__ volatile("lgdt %0"::"m"(gdtr):"memory");
    u16 selector=0x28;
    __asm__ volatile("ltr %0"::"r"(selector):"memory");
    u16 cs;
    __asm__ volatile("mov %%cs,%0":"=r"(cs));
    set_gate(0x80,wx_sys_gate,cs);
    idt[0x80].flags=0xee; /* DPL3 interrupt gate. */
    set_gate(13,wx_user_gp,cs);
    set_gate(14,wx_user_pf,cs);
    return 1;
}
static int wx_ring3_test(void) {
    if(wx_active || !pg_ready)return 0;
    static int initialized;
    if(!initialized){if(!wx_prepare_tss())return 0;initialized=1;}
    void *code=page_alloc(),*stack=page_alloc();
    if(!code||!stack){if(code)page_free(code);if(stack)page_free(stack);return 0;}
    if(!pg_map_user(WX_USER_CODE,(u64)code,1,0)||
       !pg_map_user(WX_USER_STACK,(u64)stack,1,0))return 0;
    /* Guest code: write(1,message,length), exit(42); both use int 0x80. */
    const char message[]="RING3_USER_WRITE_OK\n";
    u8 *p=(u8*)WX_USER_CODE;
    unsigned n=0;
    p[n++]=0xb8;*(u32*)(p+n)=WX_NATIVE_CONSOLE_WRITE;n+=4;
    p[n++]=0xbf;*(u32*)(p+n)=WX_NATIVE_STDOUT_HANDLE;n+=4;
    p[n++]=0x48;p[n++]=0xbe;*(u64*)(p+n)=WX_USER_CODE+0x100;n+=8;
    p[n++]=0xba;*(u32*)(p+n)=sizeof(message)-1;n+=4;
    p[n++]=0xcd;p[n++]=0x80;               /* int 80h */
    p[n++]=0xb8;*(u32*)(p+n)=WX_NATIVE_EXIT;n+=4;
    p[n++]=0xbf;*(u32*)(p+n)=42;n+=4;
    p[n++]=0xcd;p[n++]=0x80;
    p[n++]=0x0f;p[n++]=0x0b;              /* unexpected path: UD2 */
    for(u32 i=0;i<sizeof(message);i++)p[0x100+i]=message[i];
    if(!pg_protect_user(WX_USER_CODE,0,1))return 0;
    wx_active=1;wx_exit_requested=0;wx_exit_code=0;wx_fault_vector=0;
    wx_write_count=0;
    u32 result=wx_enter_user(WX_USER_CODE,WX_USER_STACK+4096-16);
    wx_active=0;
    int ok=result==42 && wx_write_count==sizeof(message)-1 && !wx_fault_vector;
    /* Second, deliberately read a supervisor-only physical identity page. */
    if(!pg_protect_user(WX_USER_CODE,1,0))ok=0;
    pg_zero(p,32);
    p[0]=0x48;p[1]=0xb8;*(u64*)(p+2)=0x1000; /* movabs rax,0x1000 */
    p[10]=0x48;p[11]=0x8b;p[12]=0x18;        /* mov rbx,[rax] */
    p[13]=0x0f;p[14]=0x0b;
    if(!pg_protect_user(WX_USER_CODE,0,1))ok=0;
    wx_active=1;wx_exit_requested=0;wx_exit_code=0;wx_fault_vector=0;
    result=wx_enter_user(WX_USER_CODE,WX_USER_STACK+4096-16);
    wx_active=0;
    if(result!=98||wx_fault_vector!=14||(wx_fault_error&4ULL)==0)ok=0;
    /* In/Out are privileged while IOPL=0 and TSS I/O permission map is disabled. */
    if(!pg_protect_user(WX_USER_CODE,1,0))ok=0;
    pg_zero(p,32);
    p[0]=0xe4;p[1]=0x80; /* in al,0x80: must raise #GP in CPL3 */
    p[2]=0x0f;p[3]=0x0b;
    if(!pg_protect_user(WX_USER_CODE,0,1))ok=0;
    wx_active=1;wx_exit_requested=0;wx_exit_code=0;wx_fault_vector=0;
    result=wx_enter_user(WX_USER_CODE,WX_USER_STACK+4096-16);
    wx_active=0;
    if(result!=97||wx_fault_vector!=13)ok=0;
    pg_unmap(WX_USER_CODE);pg_unmap(WX_USER_STACK);
    page_free(code);page_free(stack);
    if(ok)print("RING3_TEST_OK: CPL3 syscall write/exit, supervisor page access denied, kernel survived\n");
    else print("RING3_TEST_FAILED\n");
    return ok;
}
