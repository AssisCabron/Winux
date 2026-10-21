/* Real PIT IRQ0 preemption of non-cooperative CPL3 code, isolated experiment.
 * Reuses wx_address_space, wx_user_resume_frame, existing TSS / INT80 gate.
 * One CPU, one CPL3 thread executing at a time. Kernel IRQs are not switched;
 * never interpret this as SMP or a general purpose preemptive scheduler.
 */
#ifndef WINUX_TIMER_PREEMPTION_PREVIEW_H
#define WINUX_TIMER_PREEMPTION_PREVIEW_H

/* User frames, FPU and privileged IRQ stacks now live inside their owners
 * (wx_managed_process.thread), not in a parallel test-only thread registry. */
static volatile u32 wx_timer_interruptions,wx_timer_switches;
static volatile u32 wx_timer_preempt_triggered;
static u64 wx_timer_last_ip[2],wx_timer_last_rflags[2];
static u32 wx_timer_last_mxcsr[2],wx_timer_exit_codes[2];
/* One kernel-FPU save area for the single test CPU. User FXSTATE is per PID. */
static u8 wx_timer_kernel_fx[512] __attribute__((aligned(16)));
static volatile u32 wx_timer_stack_error;

__attribute__((used,noinline)) static u32 wx_timer_preempt_dispatch(u64 *frame) {
    timer_ticks++;
    if(!frame || !wx_active || !wx_proc_current_irq ||
       (frame[16]&3ULL)!=3ULL)return 0;
    /* An involuntary IRQ must preserve RAX exactly. Unlike YIELD,
     * there is no syscall return value to write into user registers. */
    u64 frame_va=(u64)frame;
    wx_proc_thread *thread=&wx_proc_current_irq->thread;
    u64 stack_base=(u64)thread->kernel_stack;
    if(frame_va<stack_base || frame_va+20*8>stack_base+sizeof(thread->kernel_stack))
        wx_timer_stack_error=1;
    for(u32 i=0;i<15;i++)thread->frame.regs[i]=frame[i];
    thread->frame.rip=frame[15];
    thread->frame.rflags=frame[17];
    thread->frame.rsp=frame[18];
    thread->interrupt_count++;
    wx_timer_interruptions++;
    wx_timer_preempt_triggered=1;
    wx_exit_requested=1;wx_exit_code=0;
    return 1;
}

/* Hardware frame on CPL3 transition: RIP,CS,RFLAGS,RSP,SS.
 * IRQ0 DPL0 / interrupt gate clears IF on kernel entry.
 * If interrupting CPL0, ACK on that stack and IRET, no C call.
 * If CPL3, push all 15 GPRs, send EOI once, snapshot frame and then
 * return on wx_saved_rsp (the privileged scheduler's kernel stack).
 * Cannot preempt an arbitrary kernel critical section.
 */
__attribute__((naked,used)) static void wx_timer_irq_gate(void) {
    __asm__ volatile(
      "testb $3,8(%rsp); jz 2f;"
      "pushq %r15; pushq %r14; pushq %r13; pushq %r12; pushq %r11;"
      "pushq %r10; pushq %r9; pushq %r8; pushq %rdi; pushq %rsi;"
      "pushq %rbp; pushq %rbx; pushq %rdx; pushq %rcx; pushq %rax;"
      "movq %rsp,%rcx; subq $32,%rsp; call wx_timer_preempt_dispatch; addq $32,%rsp;"
      "movl %eax,%edx; movb $0x20,%al; outb %al,$0x20;"
      "testl %edx,%edx; jne wx_ring3_restore;"
      "popq %rax; popq %rcx; popq %rdx; popq %rbx; popq %rbp;"
      "popq %rsi; popq %rdi; popq %r8; popq %r9; popq %r10;"
      "popq %r11; popq %r12; popq %r13; popq %r14; popq %r15; iretq;"
      "2: pushq %rax; incq timer_ticks(%rip); movb $0x20,%al;"
      "outb %al,$0x20; popq %rax; iretq");
}

static void wx_timer_build_busy_loop(wx_address_space *space,u32 index){
    /* CPL3 fixture: set its own MXCSR from user memory, spin without
     * YIELD, then store MXCSR AFTER being preempted and resumed:
     * movabs rbx,stack+0x100; ldmxcsr [rbx];
     * movabs rax,stack; loop: cmp byte [rax],0; jz loop
     * stmxcsr [rbx+4]; exit(expected_code).
     * Different MXCSR values must survive switches between processes. */
    u8 *b=(u8*)space->code;
    u8 *user_stack=(u8*)space->stack;
    pg_zero(b,4096);
    pg_zero(user_stack,4096);
    u32 expected_mxcsr=index==0?0x1f80U:0x3f80U;
    *(u32*)(user_stack+0x100)=expected_mxcsr;
    u32 n=0;
    b[n++]=0x48;b[n++]=0xbb;*(u64*)(b+n)=WX_USER_STACK+0x100;n+=8;
    b[n++]=0x0f;b[n++]=0xae;b[n++]=0x13; /* ldmxcsr [rbx] */
    b[n++]=0x48;b[n++]=0xb8;*(u64*)(b+n)=WX_USER_STACK;n+=8;
    b[n++]=0x80;b[n++]=0x38;b[n++]=0;  /* cmp byte [rax],0 */
    b[n++]=0x74;b[n++]=0xfb;             /* jz back to cmp */
    b[n++]=0x0f;b[n++]=0xae;b[n++]=0x5b;b[n++]=4; /* stmxcsr [rbx+4] */
    b[n++]=0xb8;*(u32*)(b+n)=WX_NATIVE_EXIT;n+=4;
    b[n++]=0xbf;*(u32*)(b+n)=space->exit_code;n+=4;
    b[n++]=0xcd;b[n++]=0x80;
    b[n++]=0x0f;b[n++]=0x0b;
}

static int wx_timer_execute(wx_managed_process *process,u32 index,int resume) {
    if(!process || index>=2 || !wx_sched_kernel_map_ok(&process->space) ||
       wx_active || wx_user_resume_target || wx_proc_current_irq)return 0;
    wx_address_space *space=&process->space;
    wx_proc_thread *thread=&process->thread;
    u64 old_cr3=wx_read_cr3();
    wx_proc_current_irq=process;
    wx_user_resume_target=&thread->frame;
    wx_user_yielded=0;
    wx_timer_preempt_triggered=0;
    wx_exit_requested=0;wx_exit_code=0;
    wx_fault_vector=0;wx_fault_error=0;
    wx_active=1;
    /* Each managed thread now owns its TSS RSP0 kernel stack. */
    wx_tss.rsp0=((u64)thread->kernel_stack+sizeof(thread->kernel_stack)-16)&~15ULL;
    __asm__ volatile("fxsave64 %0":"=m"(wx_timer_kernel_fx)::"memory");
    wx_load_cr3((u64)space->root);
    if(resume)__asm__ volatile("fxrstor64 %0"::"m"(thread->fxstate):"memory");
    u32 result=resume?wx_resume_user(&thread->frame):
                      wx_enter_user(WX_USER_CODE,WX_USER_STACK+4096-16);
    __asm__ volatile("fxsave64 %0":"=m"(thread->fxstate)::"memory");
    __asm__ volatile("fxrstor64 %0"::"m"(wx_timer_kernel_fx):"memory");
    wx_load_cr3(old_cr3);
    wx_active=0;wx_user_resume_target=0;wx_proc_current_irq=0;
    if(resume)thread->resume_count++;
    if(wx_fault_vector)return 0;
    if(!resume){
        if(!wx_timer_preempt_triggered || wx_user_yielded ||
           !wx_exit_requested || result)return 0;
        u64 rip=thread->frame.rip;
        u64 flags=thread->frame.rflags;
        wx_timer_last_ip[index]=rip;
        wx_timer_last_rflags[index]=flags;
        wx_timer_last_mxcsr[index]=*(u32*)(thread->fxstate+24);
        if(rip<WX_USER_CODE+23 || rip>WX_USER_CODE+28 ||
           !(flags&0x200ULL) || (flags&0x3000ULL) ||
           wx_timer_last_mxcsr[index]!=(index==0?0x1f80U:0x3f80U))
            return 0;
        return 1;
    }
    wx_timer_exit_codes[index]=result;
    u32 observed_user_mxcsr=*(u32*)((u8*)space->stack+0x104);
    return !wx_timer_preempt_triggered && !wx_user_yielded &&
           wx_exit_requested && result==space->exit_code &&
           observed_user_mxcsr==(index==0?0x1f80U:0x3f80U);
}

/* The preemptive IRQ test runs through the SAME managed-process table
 * as synchronous processes. This is not a second PID or handle registry. */
static int wx_timer_managed_step(wx_managed_process *p,u32 index,int resume){
    if(!p || p->state!=WX_PROC_READY)return 0;
    p->state=WX_PROC_RUNNING;
    int success=wx_timer_execute(p,index,resume);
    p->state=success?(resume?WX_PROC_EXITED:WX_PROC_READY):WX_PROC_FAILED;
    return success;
}
static int wx_timer_preemption_test(void){
    if(wx_active || !pg_ready)return 0;
    if(!wx_prepare_tss())return 0;
    u64 original_cr3=wx_read_cr3();
    u32 tables_before=pg_used;
    u32 free_pages_before=wx_proc_free_pages();
    u16 cs;
    __asm__ volatile("mov %%cs,%0":"=r"(cs));
    idt_gate old_irq=idt[32];
    u64 old_rsp0=wx_tss.rsp0;
    wx_timer_stack_error=0;
    int ok=1;
    wx_managed_process *first=0,*second=0;
    wx_proc_current_irq=0;
    wx_timer_interruptions=0;wx_timer_switches=0;
    first=wx_proc_spawn(21);
    if(first)second=wx_proc_spawn(42);
    ok=first && second && first->pid!=second->pid &&
       first->space.root!=second->space.root &&
       wx_sched_kernel_map_ok(&first->space) &&
       wx_sched_kernel_map_ok(&second->space);
    if(!ok)goto done;
    wx_timer_build_busy_loop(&first->space,0);
    wx_timer_build_busy_loop(&second->space,1);
    /* Opt-in interrupt handler for the test only, never the base kernel. */
    set_gate(32,wx_timer_irq_gate,cs);
    /* A is interrupted by PIT even though it NEVER calls YIELD. */
    ok=wx_timer_managed_step(first,0,0);
    if(ok){wx_timer_switches++;ok=wx_timer_managed_step(second,1,0);}
    /* Scheduler writes individual stop flags in distinct physical pages. */
    if(ok){
        wx_timer_switches++;
        ((volatile u8*)first->space.stack)[0]=1;
        ((volatile u8*)second->space.stack)[0]=1;
        ok=wx_timer_managed_step(second,1,1);
    }
    if(ok){wx_timer_switches++;ok=wx_timer_managed_step(first,0,1);}
    if(ok)ok=wx_timer_interruptions>=2 && wx_timer_switches==3 &&
             wx_timer_exit_codes[0]==21 && wx_timer_exit_codes[1]==42 &&
             first->state==WX_PROC_EXITED && second->state==WX_PROC_EXITED &&
             first->thread.interrupt_count>=1 && second->thread.interrupt_count>=1 &&
             first->thread.resume_count==1 && second->thread.resume_count==1 &&
             !wx_timer_stack_error;
done:
    /* Restore original IRQ0 before any other console/VM regression. */
    __asm__ volatile("cli":::"memory");
    idt[32]=old_irq;
    wx_tss.rsp0=old_rsp0;
    wx_load_cr3(original_cr3);
    wx_active=0;wx_user_resume_target=0;wx_proc_current_irq=0;
    if(first){
        if(first->state!=WX_PROC_EXITED)first->state=WX_PROC_FAILED;
        if(!wx_proc_reap(first))ok=0;
    }
    if(second){
        if(second->state!=WX_PROC_EXITED)second->state=WX_PROC_FAILED;
        if(!wx_proc_reap(second))ok=0;
    }
    if(pg_used!=tables_before || wx_proc_free_pages()!=free_pages_before ||
       wx_proc_current_irq)ok=0;
    __asm__ volatile("sti":::"memory");
    if(ok){
        print("PIT_PREEMPT_OK: IRQ32 forced two non-yielding CPL3 loops, two CR3, restored RIP/RFLAGS, exits 42/21\n");
        print("PIT_PREEMPT_STATS: interrupts=");number(wx_timer_interruptions);
        print(" context_switches=");number(wx_timer_switches);print("\n");
        print("PIT_THREAD_STACKS_OK: separate privileged RSP0 for each CPL3 thread\n");
        print("PIT_THREAD_FPU_OK: distinct CPL3 MXCSR values preserved across IRQs and resumed instructions\n");
        print("PIT_PROC_MANAGER_OK: IRQ preemption used managed PIDs, NT handle ownership and full process reaping\n");
        print("PIT_THREAD_OWNERSHIP_OK: all GPR, FPU and RSP0 storage belongs to managed processes\n");
        print("PIT_RESOURCE_REAP_OK: IRQ-managed processes restored physical free-page count and kernel table pool\n");
    }else{
        print("PIT_PREEMPT_FAILED: interrupts=");number(wx_timer_interruptions);
        print(" switches=");number(wx_timer_switches);print("\n");
    }
    return ok;
}
#endif
