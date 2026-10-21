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
static volatile u64 wx_saved_kernel_rflags __attribute__((used));
static volatile u64 wx_fault_vector,wx_fault_error;
static volatile u32 wx_active,wx_exit_code,wx_write_count,wx_exit_requested;
#ifdef WINUX_USER_SCHED_PREVIEW
/* Snapshot of all registers as pushed by wx_sys_gate, plus the user iret frame.
 * Only one user guest is inside this kernel at a time. Each task owns its
 * saved frame; the global points at the running task's frame during a trap. */
typedef struct {
    u64 regs[15];
    u64 rip,rsp,rflags;
} wx_user_resume_frame;
static wx_user_resume_frame *wx_user_resume_target;
static volatile u32 wx_user_yielded;
#endif
/* Kernel faults remain fatal; only faults originating in CPL3 are recovered. */

__attribute__((naked,used)) static void wx_ring3_restore(void) {
    __asm__ volatile(
      "movq wx_saved_rsp(%rip),%rsp;"
      "movw $16,%ax; movw %ax,%ds; movw %ax,%es; movw %ax,%fs; movw %ax,%gs;"
      "movl wx_exit_code(%rip),%eax;"
      "popq %r15; popq %r14; popq %r13; popq %r12;"
      "popq %rsi; popq %rdi; popq %rbx; popq %rbp;"
      /* IRETQ does not run when we intentionally leave a CPL3 syscall.
       * Restore the prior kernel flags, especially IF, before RET. */
      "pushq wx_saved_kernel_rflags(%rip); popfq; ret");
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
/* Verify the CURRENT CR3 and all four user/supervisor boundaries, not
 * the global kernel root (which would validate the wrong process mapping). */
static int wx_user_readable(u64 addr,u64 size) {
    if (!size || size>WX_NATIVE_MAX_WRITE || addr<WX_USER_CODE ||
        size>4096 || addr>WX_USER_CODE+4096-size)return 0;
    u64 cr3;
    __asm__ volatile("mov %%cr3,%0":"=r"(cr3));
    u64 *table=(u64*)(cr3&PG_MASK);
    const unsigned shifts[]={39,30,21};
    for(unsigned i=0;i<3;i++){
        u64 value=table[(addr>>shifts[i])&511];
        if((value&(PG_PRESENT|PG_USER))!=(PG_PRESENT|PG_USER) ||
           (value&128ULL))return 0;
        table=(u64*)(value&PG_MASK);
    }
    u64 leaf=table[(addr>>12)&511];
    return (leaf&(PG_PRESENT|PG_USER))==(PG_PRESENT|PG_USER);
}
#ifdef WINUX_SERVICE_PREVIEW
/* Current service identity is set by the existing CPL3 service scheduler,
 * never provided by a userspace argument. Single CPU / no SMP. */
static u32 wx_current_service_pid;
static u64 wx_service_ipc_send(u32 sender_pid,u32 receiver_pid,u64 value);
static u64 wx_service_ipc_receive(u32 receiver_pid);
#endif
/* Frame saved in order rax, rcx, rdx, rbx, rbp, rsi, rdi, r8 ... r15. */
__attribute__((used,noinline)) static void wx_sys_dispatch(u64 *frame) {
    u64 number=frame[0],arg0=frame[6],arg1=frame[5],arg2=frame[2];
#ifdef WINUX_USER_SCHED_PREVIEW
    if(number==WX_NATIVE_YIELD && wx_user_resume_target){
        /* The guest sees 0 as the return value of yield when it resumes. */
        frame[0]=0;
        for(unsigned i=0;i<15;i++)wx_user_resume_target->regs[i]=frame[i];
        wx_user_resume_target->rip=frame[15];
        wx_user_resume_target->rflags=frame[17];
        wx_user_resume_target->rsp=frame[18];
        wx_user_yielded=1;
        wx_exit_requested=1;
        wx_exit_code=0;
        return;
    }
#endif
#ifdef WINUX_SERVICE_PREVIEW
    if (number==WX_NATIVE_IPC_SEND) {
        frame[0]=wx_service_ipc_send(wx_current_service_pid,(u32)arg0,arg1);
        return;
    }
    if (number==WX_NATIVE_IPC_RECEIVE) {
        frame[0]=wx_service_ipc_receive(wx_current_service_pid);
        return;
    }
#endif
    if (number==WX_NATIVE_CLOCK_TICKS) {
        frame[0]=timer_ticks;
        return;
    }
    if (number==WX_NATIVE_MEMORY_FREE_PAGES) {
        u32 free_pages=0;
        for(u32 i=0;i<PAGE_POOL_COUNT;i++)if(!page_used[i])free_pages++;
        frame[0]=free_pages;
        return;
    }
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
      "pushfq; popq wx_saved_kernel_rflags(%rip);"
      "movw $0x1b,%ax; movw %ax,%ds; movw %ax,%es; movw %ax,%fs; movw %ax,%gs;"
      "pushq $0x1b; pushq %rdx; pushfq;"
      "orq $0x200,(%rsp); andq $-1025,(%rsp);"
      "pushq $0x23; pushq %rcx; iretq");
}
#ifdef WINUX_USER_SCHED_PREVIEW
/* C caller explicitly uses Microsoft x64 ABI: argument in RCX. The frame
 * restores ALL user GPRs and the 5-word ring-3 IRETQ return frame.
 * Kernel nonvolatile registers are parked for wx_ring3_restore, as in
 * wx_enter_user. This is the cooperative syscall boundary, not IRQ preemption. */
__attribute__((naked,noinline,ms_abi)) static u32
wx_resume_user(const wx_user_resume_frame *frame) {
    __asm__ volatile(
      "pushq %rbp; pushq %rbx; pushq %rdi; pushq %rsi;"
      "pushq %r12; pushq %r13; pushq %r14; pushq %r15;"
      "movq %rsp,wx_saved_rsp(%rip);"
      "pushfq; popq wx_saved_kernel_rflags(%rip);"
      "movq %rcx,%rbx;"
      "movw $0x1b,%ax; movw %ax,%ds; movw %ax,%es; movw %ax,%fs; movw %ax,%gs;"
      "pushq $0x1b; pushq 128(%rbx);"
      "pushq 136(%rbx); pushq $0x23; pushq 120(%rbx);"
      "movq 0(%rbx),%rax; movq 16(%rbx),%rdx; movq 32(%rbx),%rbp;"
      "movq 40(%rbx),%rsi; movq 48(%rbx),%rdi;"
      "movq 56(%rbx),%r8; movq 64(%rbx),%r9; movq 72(%rbx),%r10;"
      "movq 80(%rbx),%r11; movq 88(%rbx),%r12; movq 96(%rbx),%r13;"
      "movq 104(%rbx),%r14; movq 112(%rbx),%r15;"
      "movq 8(%rbx),%rcx; movq 24(%rbx),%rbx; iretq");
}
#endif
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
