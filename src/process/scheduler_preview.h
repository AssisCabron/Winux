/* Cooperative x64 kernel-task scheduling preview.
 * Reuses address spaces, page allocator and the Ring3 context groundwork.
 * Saves callee-saved Win64 registers, FPU/SSE, RSP and CR3 across REAL stack
 * switches. Explicit yield points only; not a preemptive Ring3 scheduler. */
#ifndef WINUX_SCHED_PREVIEW_H
#define WINUX_SCHED_PREVIEW_H
typedef struct __attribute__((aligned(16))) {
    u64 rsp, cr3;
    u8 fx[512] __attribute__((aligned(16)));
} wx_sched_context;
_Static_assert(__builtin_offsetof(wx_sched_context,fx)==16,"FPU save area alignment");
typedef struct {
    wx_sched_context ctx;
    u8 stack[8192] __attribute__((aligned(16)));
    u32 task_id;
    u32 state; /* 0 absent, 1 ready, 2 finished */
    /* Reuse the existing NT object core: per-task handle namespace. */
    obj_table objects;
    u64 owned_event;
#ifdef WINUX_USER_SCHED_PREVIEW
    wx_user_resume_frame user_frame;
    u8 user_stage; /* 0 not entered, 1 suspended at yield, 2 exited */
#endif
} wx_sched_task;
static wx_sched_context wx_scheduler_ctx;
static wx_sched_task wx_sched_tasks[2];
static u32 wx_sched_active,wx_sched_log[6],wx_sched_log_size;
static int wx_sched_error;
static u32 wx_sched_fpu_seen[2][2];
static u32 wx_sched_user_status[2][2];
static u32 wx_sched_user_yields[2][2];
static u32 wx_sched_user_faults[2][2];

/* Register contract explicitly owned by context_switch.S: RDI=from, RSI=to.
 * C ABI is specified as ms_abi, while the assembly's own entry register
 * convention is enforced by constraints instead of implicit RCX/RDX access.
 * This is a cooperative CALL boundary, not a timer IRQ trap frame. */
extern void wx_sched_switch_regs(void);
__attribute__((noinline,ms_abi)) static void
wx_sched_switch(wx_sched_context *from,const wx_sched_context *to){
    register wx_sched_context *source __asm__("rdi")=from;
    register const wx_sched_context *target __asm__("rsi")=to;
    __asm__ volatile("call wx_sched_switch_regs"
                     : "+D"(source),"+S"(target)
                     :
                     : "rax","rcx","rdx","r8","r9","r10","r11","memory","cc");
}
static void wx_sched_note(u32 marker){
    if(wx_sched_log_size>=6){wx_sched_error=1;return;}
    wx_sched_log[wx_sched_log_size++]=marker;
}
static void wx_sched_yield(void){
    wx_sched_task *task=&wx_sched_tasks[wx_sched_active];
    wx_sched_switch(&task->ctx,&wx_scheduler_ctx);
}
#ifdef WINUX_USER_SCHED_PREVIEW
/* Each scheduled kernel task owns the CPL3 continuation of its process.
 * A single global Ring3 entry gate is valid because only one task may be
 * in CPL3 at a time; per-thread TSS/RSP0 and IRQ preemption are future work. */
static int wx_sched_user_step(wx_sched_task *task,int resume){
    wx_user_resume_target=&task->user_frame;
    wx_user_yielded=0;wx_exit_requested=0;wx_exit_code=0;
    wx_fault_vector=0;wx_fault_error=0;wx_active=1;
    u32 status=resume?wx_resume_user(&task->user_frame):
                      wx_enter_user(WX_USER_CODE,WX_USER_STACK+4096-16);
    wx_active=0;
    wx_user_resume_target=0;
    unsigned index=task->task_id==101?0:1;
    wx_sched_user_status[index][resume]=status;
    wx_sched_user_yields[index][resume]=wx_user_yielded;
    wx_sched_user_faults[index][resume]=(u32)wx_fault_vector;
    if(wx_fault_vector)return 0;
    if(!resume){
        if(!wx_user_yielded || status!=0 || !wx_exit_requested)return 0;
        task->user_stage=1;
        return 1;
    }
    if(wx_user_yielded || !wx_exit_requested || status!=(task->task_id==101?21U:42U))return 0;
    task->user_stage=2;
    return 1;
}
#endif
static __attribute__((noreturn)) void wx_sched_entry(void){
    u32 task_index=wx_sched_active;
    wx_sched_task *task=&wx_sched_tasks[task_index];
    volatile u32 protected_local=task_index==0?0x51a0U:0x62b0U;
    /* Different MXCSR rounding modes prove FXSAVE64/FXRSTOR64 isolate the
     * per-task SIMD control state (not merely the GPR stack pointer). */
    u32 expected_mxcsr=task_index==0?0x3f80U:0x5f80U;
    __asm__ volatile("ldmxcsr %0"::"m"(expected_mxcsr):"memory");
    if(task->state!=1)wx_sched_error=1;
    u32 expected_pid=task_index==0?101U:202U;
    if(*(volatile u32*)(WX_USER_CODE+0x100)!=expected_pid)wx_sched_error=1;
    const int64_t immediate=0;
    /* B may not access A's live handle in B's still-empty table. */
    if(task_index==1 && obj_event_wait(&task->objects,wx_sched_tasks[0].owned_event,
                                       0,&immediate)!=OBJ_INVALID_HANDLE)wx_sched_error=1;
    if(obj_event_create(&task->objects,OBJ_EVENT_ALL,0,0,&task->owned_event))wx_sched_error=1;
    wx_sched_note(task_index==0?11U:22U);
    wx_sched_yield();
    if(wx_sched_active!=task_index ||
       protected_local!=(task_index==0?0x51a0U:0x62b0U))wx_sched_error=1;
    u32 resumed_mxcsr;
    u64 resumed_rflags;
    __asm__ volatile("stmxcsr %0; pushfq; popq %1"
                     :"=m"(resumed_mxcsr),"=r"(resumed_rflags)::"memory");
    wx_sched_fpu_seen[task_index][0]=resumed_mxcsr;
    if(resumed_mxcsr!=expected_mxcsr || !(resumed_rflags&0x200ULL))
        wx_sched_error=1;
    if(task_index==1){
        /* Setting A's event must not set B's event (even if handles alias numerically). */
        if(obj_event_wait(&task->objects,task->owned_event,0,&immediate)!=OBJ_TIMEOUT)
            wx_sched_error=1;
    }
    if(obj_event_set(&task->objects,task->owned_event,0))wx_sched_error=1;
#ifdef WINUX_USER_SCHED_PREVIEW
    /* The second quantum enters genuine user mode and stops on YIELD. */
    if(!wx_sched_user_step(task,0))wx_sched_error=1;
#endif
    wx_sched_note(task_index==0?13U:24U);
    wx_sched_yield();
    if(wx_sched_active!=task_index ||
       protected_local!=(task_index==0?0x51a0U:0x62b0U))wx_sched_error=1;
    __asm__ volatile("stmxcsr %0; pushfq; popq %1"
                     :"=m"(resumed_mxcsr),"=r"(resumed_rflags)::"memory");
    wx_sched_fpu_seen[task_index][1]=resumed_mxcsr;
    if(resumed_mxcsr!=expected_mxcsr || !(resumed_rflags&0x200ULL))
        wx_sched_error=1;
#ifdef WINUX_USER_SCHED_PREVIEW
    /* Third quantum resumes the saved CPL3 IRET frame at the post-yield RIP.
     * It must reach the real per-process EXIT with its own return code. */
    if(task->user_stage!=1 || !wx_sched_user_step(task,1))wx_sched_error=1;
#endif
    if(obj_event_wait(&task->objects,task->owned_event,0,&immediate) ||
       obj_close(&task->objects,task->owned_event) ||
       obj_event_wait(&task->objects,task->owned_event,0,&immediate)!=OBJ_INVALID_HANDLE)
        wx_sched_error=1;
    wx_sched_note(task_index==0?15U:26U);
    task->state=2;
    /* Yield exactly once to the kernel scheduler. It may reclaim our stack
     * only after it has resumed on the scheduler's own kernel stack. */
    wx_sched_yield();
    /* Resuming a completed task is a scheduler bug: do NOT halt a CPU. */
    wx_sched_error=1;
    for(;;)__asm__ volatile("ud2");
}
static void wx_sched_task_init(unsigned i,const wx_address_space *space){
    wx_sched_task *t=&wx_sched_tasks[i];
    pg_zero(t,sizeof(*t));
    t->task_id=space->pid;t->state=1;
    t->ctx.cr3=(u64)space->root;
    __asm__ volatile("fxsave64 %0":"=m"(t->ctx.fx): :"memory");
    u64 top=((u64)t->stack+sizeof(t->stack))&~15ULL;
    /* Win64 entry RSP must be 8 mod 16; [RSP+8..+39] is 32-byte
     * caller shadow space. Our .S pops eight GPRs, POPFQ and RET:
     *   top-40 = RSP after RET (Win64 function entry)
     *   top-48 = saved RIP (wx_sched_entry)
     *   top-56 = saved RFLAGS (IF=1, IOPL=0)
     *   top-120..-57 = 8 registers in assembly pop order.
     * No unwinding or implicit return from wx_sched_entry is allowed. */
    u64 *sp=(u64*)(top-40);
    *(--sp)=(u64)wx_sched_entry;
    *(--sp)=0x202ULL;
    for(unsigned k=0;k<8;k++)*(--sp)=0;
    t->ctx.rsp=(u64)sp;
}
/* Every currently accessed kernel code/data/stack address must remain mapped
 * supervisor-only and must resolve identically through each process CR3.
 * Switching CR3 is safe here ONLY because low identity maps are inherited. */
static int wx_sched_kernel_map_ok(const wx_address_space *space){
    if(!space || !space->root || !space->pdpt)return 0;
    if(((u64)space->root&4095) || ((u64)space->pdpt&4095))return 0;
    if((space->root[0]&(PG_PRESENT|PG_USER))!=(PG_PRESENT|PG_USER))return 0;
    const u64 *original=(const u64*)(pg_root[0]&PG_MASK);
    for(unsigned i=0;i<4;i++)
        if(space->pdpt[i]!=original[i] || (space->pdpt[i]&PG_USER))return 0;
    const u64 pointers[]={
        (u64)wx_sched_switch_regs,(u64)wx_sched_switch,
        (u64)wx_sched_entry,(u64)wx_sched_tasks,(u64)wx_sched_log,
        (u64)&wx_sched_error,(u64)wx_spaces,(u64)pg_tables,
        (u64)wx_scheduler_ctx.fx,(u64)wx_sched_tasks[0].stack,
        (u64)wx_sched_tasks[1].stack,(u64)&nt_objects,
        (u64)wx_sys_stack,(u64)&wx_tss
    };
    for(unsigned i=0;i<sizeof(pointers)/sizeof(pointers[0]);i++){
        u64 va=pointers[i];
        if(!pg_low(va,1))return 0;
        u32 a=(u32)(va>>30),b=(u32)((va>>21)&511);
        if(a>=4)return 0;
        const u64 *pd=(const u64*)(space->pdpt[a]&PG_MASK);
        if(!(pd[b]&PG_PRESENT) || (pd[b]&PG_USER))return 0;
    }
    return 1;
}
static int wx_scheduler_test(void){
    if(wx_active||!pg_ready)return 0;
#ifdef WINUX_USER_SCHED_PREVIEW
    static int user_gate_ready;
    if(!user_gate_ready){
        if(!wx_prepare_tss())return 0;
        user_gate_ready=1;
    }
#endif
    u64 cr0,cr4,flags;
    __asm__ volatile("mov %%cr0,%0; mov %%cr4,%1; pushfq; popq %2"
                     :"=r"(cr0),"=r"(cr4),"=r"(flags));
    /* FXSAVE64/FXRSTOR64 require CR0.MP, !CR0.EM, CR4.OSFXSR,
     * CR4.OSXMMEXCPT and a 16-byte aligned save area. One CPU only. */
    if((cr0&0x2ULL)==0 || (cr0&0x4ULL) ||
       (cr4&0x600ULL)!=0x600ULL ||
       (__builtin_offsetof(wx_sched_context,fx)&15) ||
       ((u64)&wx_scheduler_ctx.fx&15) ||
       (flags&0x200ULL)==0)return 0;
    const u64 original_cr3=wx_read_cr3();
    const u32 pages_before=pg_used;
    const u32 unused_before=wx_sched_log_size;
    (void)unused_before;
    pg_zero(wx_spaces,sizeof(wx_spaces));
    int ok=wx_space_make(&wx_spaces[0],101,21) &&
           wx_space_make(&wx_spaces[1],202,42);
    if(ok)ok=wx_sched_kernel_map_ok(&wx_spaces[0])&&
             wx_sched_kernel_map_ok(&wx_spaces[1]);
    if(!ok)goto cleanup;
    wx_sched_task_init(0,&wx_spaces[0]);
    wx_sched_task_init(1,&wx_spaces[1]);
    wx_scheduler_ctx.cr3=original_cr3;
    wx_sched_log_size=0;wx_sched_error=0;
    /* Round robin is driven by real cooperative yields, not by a fake log. */
    for(unsigned turn=0;turn<6;turn++){
        unsigned task=turn&1U;
        if(wx_sched_tasks[task].state!=1){ok=0;break;}
        wx_sched_active=task;
        wx_sched_switch(&wx_scheduler_ctx,&wx_sched_tasks[task].ctx);
        if(wx_read_cr3()!=original_cr3){ok=0;break;}
        if(wx_sched_error){ok=0;break;}
    }
    if(wx_sched_tasks[0].state!=2||wx_sched_tasks[1].state!=2||
       wx_sched_log_size!=6)ok=0;
#ifdef WINUX_USER_SCHED_PREVIEW
    if(wx_sched_tasks[0].user_stage!=2||wx_sched_tasks[1].user_stage!=2)ok=0;
#endif
    static const u32 expected[]={11,22,13,24,15,26};
    for(unsigned i=0;i<6;i++)if(wx_sched_log[i]!=expected[i])ok=0;
    for(unsigned t=0;t<2;t++)for(unsigned k=0;k<OBJ_SLOTS;k++)
        if(wx_sched_tasks[t].objects.slots[k].kind)ok=0;
cleanup:
    wx_load_cr3(original_cr3);
    for(unsigned i=0;i<WX_SPACE_COUNT;i++)
        if(wx_spaces[i].root && !wx_space_destroy(&wx_spaces[i]))ok=0;
    if(pg_used!=pages_before)ok=0;
    /* Only the scheduler kernel stack is live now. It is safe to scrub
     * finished/abandoned task stacks, saved GPRs and task-owned object tables.
     * Never free a stack while a CPU is still executing on it. */
    pg_zero(wx_sched_tasks,sizeof(wx_sched_tasks));
    pg_zero(&wx_scheduler_ctx,sizeof(wx_scheduler_ctx));
    for(unsigned i=0;i<sizeof(wx_sched_tasks);i++)
        if(((u8*)wx_sched_tasks)[i])ok=0;
    if(ok){print("SCHED_CONTEXT_OK: 2 task stacks, Win64 preserved regs, FPU, 2 CR3, 6 yields, exit teardown\n");
        print("SCHED_OBJECT_NAMESPACE_OK: 2 independent NT handle tables, cross-table rejection, teardown\n");
        print("SCHED_FPU_FLAGS_OK: task-specific MXCSR, IF preserved, assembly RFLAGS switch\n");
#ifdef WINUX_USER_SCHED_PREVIEW
        print("CPL3_SCHED_OK: 2 processes resumed after native yield, isolated CR3, exits 21/42\n");
#endif
    }
    else {
        print("SCHED_CONTEXT_FAILED\n");
        for(unsigned index=0;index<2;index++){
            print("SCHED_DIAG task=");number(index);
            print(" mxcsr_first=");number(wx_sched_fpu_seen[index][0]);
            print(" mxcsr_second=");number(wx_sched_fpu_seen[index][1]);
            print(" user_enter=");number(wx_sched_user_status[index][0]);
            print(" yielded=");number(wx_sched_user_yields[index][0]);
            print(" resumed=");number(wx_sched_user_status[index][1]);
            print(" yielded_again=");number(wx_sched_user_yields[index][1]);
            print(" fault0=");number(wx_sched_user_faults[index][0]);
            print(" fault1=");number(wx_sched_user_faults[index][1]);
            print("\n");
        }
    }
    return ok;
}
#endif
