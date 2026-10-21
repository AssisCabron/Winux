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
} wx_sched_task;
static wx_sched_context wx_scheduler_ctx;
static wx_sched_task wx_sched_tasks[2];
static u32 wx_sched_active,wx_sched_log[6],wx_sched_log_size;
static int wx_sched_error;

__attribute__((naked,noinline)) static void wx_sched_switch(wx_sched_context *from,const wx_sched_context *to){
    __asm__ volatile(
      "pushq %rbp; pushq %rbx; pushq %rdi; pushq %rsi;"
      "pushq %r12; pushq %r13; pushq %r14; pushq %r15;"
      "movq %rsp,0(%rcx); movq %cr3,%rax; movq %rax,8(%rcx);"
      "fxsave64 16(%rcx);"
      "movq 8(%rdx),%rax; movq %rax,%cr3;"
      "fxrstor64 16(%rdx); movq 0(%rdx),%rsp;"
      "popq %r15; popq %r14; popq %r13; popq %r12;"
      "popq %rsi; popq %rdi; popq %rbx; popq %rbp; retq");
}
static void wx_sched_note(u32 marker){
    if(wx_sched_log_size>=6){wx_sched_error=1;return;}
    wx_sched_log[wx_sched_log_size++]=marker;
}
static void wx_sched_yield(void){
    wx_sched_task *task=&wx_sched_tasks[wx_sched_active];
    wx_sched_switch(&task->ctx,&wx_scheduler_ctx);
}
static __attribute__((noreturn)) void wx_sched_entry(void){
    u32 task_index=wx_sched_active;
    wx_sched_task *task=&wx_sched_tasks[task_index];
    volatile u32 protected_local=task_index==0?0x51a0U:0x62b0U;
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
    if(task_index==1){
        /* Setting A's event must not set B's event (even if handles alias numerically). */
        if(obj_event_wait(&task->objects,task->owned_event,0,&immediate)!=OBJ_TIMEOUT)
            wx_sched_error=1;
    }
    if(obj_event_set(&task->objects,task->owned_event,0))wx_sched_error=1;
    wx_sched_note(task_index==0?13U:24U);
    wx_sched_yield();
    if(wx_sched_active!=task_index ||
       protected_local!=(task_index==0?0x51a0U:0x62b0U))wx_sched_error=1;
    if(obj_event_wait(&task->objects,task->owned_event,0,&immediate) ||
       obj_close(&task->objects,task->owned_event) ||
       obj_event_wait(&task->objects,task->owned_event,0,&immediate)!=OBJ_INVALID_HANDLE)
        wx_sched_error=1;
    wx_sched_note(task_index==0?15U:26U);
    task->state=2;
    wx_sched_yield();
    for(;;)__asm__ volatile("hlt");
}
static void wx_sched_task_init(unsigned i,const wx_address_space *space){
    wx_sched_task *t=&wx_sched_tasks[i];
    pg_zero(t,sizeof(*t));
    t->task_id=space->pid;t->state=1;
    t->ctx.cr3=(u64)space->root;
    __asm__ volatile("fxsave64 %0":"=m"(t->ctx.fx): :"memory");
    u64 top=((u64)t->stack+sizeof(t->stack))&~15ULL;
    u64 *sp=(u64*)(top-40); /* reserve 32-byte Win64 shadow space above function entry */
    *(--sp)=(u64)wx_sched_entry;
    for(unsigned k=0;k<8;k++)*(--sp)=0;
    t->ctx.rsp=(u64)sp;
}
static int wx_scheduler_test(void){
    if(wx_active||!pg_ready)return 0;
    const u64 original_cr3=wx_read_cr3();
    const u32 pages_before=pg_used;
    const u32 unused_before=wx_sched_log_size;
    (void)unused_before;
    pg_zero(wx_spaces,sizeof(wx_spaces));
    int ok=wx_space_make(&wx_spaces[0],101,21) &&
           wx_space_make(&wx_spaces[1],202,42);
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
    static const u32 expected[]={11,22,13,24,15,26};
    for(unsigned i=0;i<6;i++)if(wx_sched_log[i]!=expected[i])ok=0;
    for(unsigned t=0;t<2;t++)for(unsigned k=0;k<OBJ_SLOTS;k++)
        if(wx_sched_tasks[t].objects.slots[k].kind)ok=0;
cleanup:
    wx_load_cr3(original_cr3);
    for(unsigned i=0;i<WX_SPACE_COUNT;i++){
        if(wx_spaces[i].code)page_free(wx_spaces[i].code);
        if(wx_spaces[i].stack)page_free(wx_spaces[i].stack);
        pg_zero(&wx_spaces[i],sizeof(wx_spaces[i]));
    }
    for(u32 i=pages_before;i<pg_used;i++)pg_zero(pg_tables[i],4096);
    pg_used=pages_before;
    if(ok){print("SCHED_CONTEXT_OK: 2 task stacks, Win64 preserved regs, FPU, 2 CR3, 6 yields, exit teardown\n");
        print("SCHED_OBJECT_NAMESPACE_OK: 2 independent NT handle tables, cross-table rejection, teardown\n");}
    else print("SCHED_CONTEXT_FAILED\n");
    return ok;
}
#endif
