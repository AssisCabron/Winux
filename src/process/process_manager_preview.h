/* Minimal native process manager with REAL CPL3 execution and full reaping.
 * Reuses the existing address-space allocator, NT object table and INT80
 * loader fixture. A process slot is NOT a fake Task Manager row.
 * Single-core synchronous initial process executions, not generic PE loading.
 */
#ifndef WINUX_PROCESS_MANAGER_PREVIEW_H
#define WINUX_PROCESS_MANAGER_PREVIEW_H
#define WX_PROC_CAPACITY 6U
#define WX_PROC_TRANSIENT_COUNT 4U
#define WX_PROC_SERVICE_OFFSET 4U
enum {WX_PROC_FREE=0,WX_PROC_READY=1,WX_PROC_RUNNING=2,
      WX_PROC_EXITED=3,WX_PROC_FAILED=4,WX_PROC_BLOCKED=5};
enum {WX_WAIT_NONE=0,WX_WAIT_TIMER=1,WX_WAIT_IPC=2};
/* One managed thread per CPL3 process in this phase. Its user trap-frame,
 * x87/SSE state and TSS RSP0 stack must have the same lifecycle as the PID.
 * Future multiple threads can extend this structure without parallel tables. */
#ifdef WINUX_TIMER_PREEMPT_PREVIEW
typedef struct {
    wx_user_resume_frame frame;
    u8 fxstate[512] __attribute__((aligned(16)));
    u8 kernel_stack[16384] __attribute__((aligned(16)));
    u32 interrupt_count;
    u32 resume_count;
    u32 started;
    u32 tid;
} wx_proc_thread;
_Static_assert(__builtin_offsetof(wx_proc_thread,fxstate)%16==0,
               "FXSAVE state must be 16-byte aligned");
#endif
typedef struct {
    wx_address_space space;
    obj_table objects;
    u64 owned_event;
    u32 pid,state,exit_code;
    u32 resident,quanta;
    u32 wait_kind;
    u64 next_ready_tick;
    const char *name;
#ifdef WINUX_TIMER_PREEMPT_PREVIEW
    wx_proc_thread thread;
    wx_proc_thread sibling;
    void *sibling_user_stack;
#endif
} wx_managed_process;
static wx_managed_process wx_process_table[WX_PROC_CAPACITY];
#ifdef WINUX_TIMER_PREEMPT_PREVIEW
static wx_managed_process *wx_proc_current_irq;
static wx_proc_thread *wx_proc_current_thread;
#endif
static u32 wx_next_process_id=600;
static u32 wx_proc_rr_cursor;
static u32 wx_proc_sleep_calls,wx_proc_wake_count,wx_proc_invalid_sleeps;
static u32 wx_proc_ipc_waits,wx_proc_ipc_wakes,wx_proc_ipc_timeouts;

static u32 wx_proc_free_pages(void){
    u32 available=0;
    for(u32 i=0;i<PAGE_POOL_COUNT;i++)if(!page_used[i])available++;
    return available;
}
static wx_managed_process *wx_proc_spawn(u32 exit_code){
    if(wx_active || wx_user_resume_target)return 0;
    for(u32 i=0;i<WX_PROC_TRANSIENT_COUNT;i++){
        wx_managed_process *p=&wx_process_table[i];
        if(p->state!=WX_PROC_FREE)continue;
        u32 pid=++wx_next_process_id;
        if(!wx_space_make(&p->space,pid,exit_code))return 0;
        if(obj_event_create(&p->objects,OBJ_EVENT_ALL,0,0,&p->owned_event)){
            wx_space_destroy(&p->space);
            pg_zero(p,sizeof(*p));
            return 0;
        }
        p->pid=pid;p->exit_code=exit_code;p->state=WX_PROC_READY;
        return p;
    }
    return 0;
}
/* Fixed resident slots share the ONLY Process Manager's PID, CR3,
 * objects and thread context. Reserved service memory survives STOP,
 * is reused on RESTART and never enters the transient job pool. */
static wx_managed_process *wx_proc_resident(unsigned index,u32 pid,const char *name){
    if(index>=2 || !pid || !name || wx_active)return 0;
    wx_managed_process *p=&wx_process_table[WX_PROC_SERVICE_OFFSET+index];
    if(p->state==WX_PROC_FREE){
        if(!wx_space_make(&p->space,pid,0))return 0;
        if(obj_event_create(&p->objects,OBJ_EVENT_ALL,0,0,&p->owned_event)){
            wx_space_destroy(&p->space);pg_zero(p,sizeof(*p));return 0;
        }
        p->pid=pid;p->name=name;p->resident=1;
#ifdef WINUX_TIMER_PREEMPT_PREVIEW
        p->thread.tid=pid*4U;
#endif
        p->state=WX_PROC_EXITED;
    }
    return (p->resident && p->pid==pid && p->name==name)?p:0;
}
static int wx_proc_alive(const wx_managed_process *p){
    return p && p->resident &&
           (p->state==WX_PROC_READY || p->state==WX_PROC_RUNNING ||
            p->state==WX_PROC_BLOCKED);
}
/* Called by the native INT80 dispatch only for a verified active CPL3
 * thread. The process's own state/next deadline is the sole sleep ledger. */
static int wx_proc_request_sleep(u64 ticks){
    wx_managed_process *p=wx_proc_current_irq;
    if(!p || !wx_active || p->state!=WX_PROC_RUNNING ||
       !ticks || ticks>1000 || timer_ticks>~0ULL-ticks){
        wx_proc_invalid_sleeps++;
        return 0;
    }
    p->next_ready_tick=timer_ticks+ticks;
    p->wait_kind=WX_WAIT_TIMER;
    p->state=WX_PROC_BLOCKED;
    wx_proc_sleep_calls++;
    return 1;
}
/* One ready-queue selection point for all resident processes.
 * Service policies set their next deadline but do NOT choose a second
 * scheduling order. This is one CPU, no priority/preempt-on-IRQ-switch yet. */
/* Release expired waits for ALL managed processes, not only services.
 * Nothing may run while state==BLOCKED. Timer tick is monotonic in this
 * current one-CPU PIT setup; overflow is rejected when sleep is requested. */
/* IPC uses the same blocked state/deadline but may be signaled early.
 * Only the active recipient process may wait; pending mailboxes are
 * checked by the service layer before entering this state. */
static int wx_proc_request_ipc_wait(u64 ticks){
    wx_managed_process *p=wx_proc_current_irq;
    if(!p || !wx_active || !p->resident || p->state!=WX_PROC_RUNNING ||
       !ticks || ticks>1000 || timer_ticks>~0ULL-ticks)return 0;
    p->state=WX_PROC_BLOCKED;
    p->wait_kind=WX_WAIT_IPC;
    p->next_ready_tick=timer_ticks+ticks;
    wx_proc_ipc_waits++;
    return 1;
}
static int wx_proc_notify_ipc(wx_managed_process *p,u64 message){
    if(!p || p->state!=WX_PROC_BLOCKED || p->wait_kind!=WX_WAIT_IPC ||
       !message)return 0;
    p->thread.frame.regs[0]=message;
    p->state=WX_PROC_READY;
    p->wait_kind=WX_WAIT_NONE;
    p->next_ready_tick=timer_ticks;
    wx_proc_ipc_wakes++;
    return 1;
}
static void wx_proc_wake_blocked(void){
    for(u32 i=0;i<WX_PROC_CAPACITY;i++){
        wx_managed_process *p=&wx_process_table[i];
        if(p->state==WX_PROC_BLOCKED && timer_ticks>=p->next_ready_tick){
            if(p->wait_kind==WX_WAIT_IPC){
                p->thread.frame.regs[0]=0;
                wx_proc_ipc_timeouts++;
            }
            p->state=WX_PROC_READY;
            p->wait_kind=WX_WAIT_NONE;
            wx_proc_wake_count++;
        }
    }
}
static wx_managed_process *wx_proc_pick_ready(void){
    wx_proc_wake_blocked();
    for(u32 attempt=0;attempt<WX_PROC_CAPACITY;attempt++){
        u32 index=(wx_proc_rr_cursor+attempt)%WX_PROC_CAPACITY;
        wx_managed_process *p=&wx_process_table[index];
        if(!p->resident || p->state!=WX_PROC_READY ||
           timer_ticks<p->next_ready_tick)continue;
        wx_proc_rr_cursor=(index+1)%WX_PROC_CAPACITY;
        return p;
    }
    return 0;
}
static void wx_proc_list(void){
    print("PROCESS_LIST_BEGIN\n");
    for(u32 i=0;i<WX_PROC_CAPACITY;i++){
        wx_managed_process *p=&wx_process_table[i];
        if(!wx_proc_alive(p) && p->state!=WX_PROC_RUNNING)continue;
        print("PROCESS pid=");number(p->pid);
        print(" name=");print(p->name?p->name:"native-task");
        print(" cpl=3 state=RUNNING cr3=");print_hex((u64)p->space.root);
        print(" quanta=");number(p->quanta);
        print(" sched_state=");print(p->state==WX_PROC_BLOCKED?"BLOCKED":
                                       p->state==WX_PROC_READY?"READY":"RUNNING");
#ifdef WINUX_TIMER_PREEMPT_PREVIEW
        print(" tid=");number(p->thread.tid);
#endif
        print("\n");
    }
    print("PROCESS_LIST_END\n");
}
static void wx_proc_thread_list(void){
    print("THREAD_LIST_BEGIN\n");
#ifdef WINUX_TIMER_PREEMPT_PREVIEW
    for(u32 i=0;i<WX_PROC_CAPACITY;i++){
        const wx_managed_process *p=&wx_process_table[i];
        if(!wx_proc_alive(p))continue;
        print("THREAD tid=");number(p->thread.tid);
        print(" pid=");number(p->pid);
        print(" state=");print(p->state==WX_PROC_BLOCKED?"BLOCKED":
                                 p->state==WX_PROC_READY?"READY":"RUNNING");
        print(" stack_rsp0=");print_hex((u64)p->thread.kernel_stack+
                                       sizeof(p->thread.kernel_stack)-16);
        print("\n");
        /* Sibling threads are only visible if one was genuinely created.
         * Do not fabricate them for each service or temporary slot. */
        if(p->sibling_user_stack){
            print("THREAD tid=");number(p->sibling.tid);
            print(" pid=");number(p->pid);
            print(" state=ALLOCATED stack_rsp0=");
            print_hex((u64)p->sibling.kernel_stack+sizeof(p->sibling.kernel_stack)-16);
            print("\n");
        }
    }
#endif
    print("THREAD_LIST_END\n");
}
/* Spawn a second CPL3 thread in the SAME address space. Only the
 * second user stack is new; code, data, CR3 and handles remain process-owned.
 * The first thread uses wx_proc_thread thread; the second uses sibling. */
static int wx_proc_add_sibling(wx_managed_process *p){
    if(!p || !p->space.root || p->sibling_user_stack || wx_active)return 0;
    u32 slot=(u32)(((WX_USER_STACK+4096ULL)>>12)&511U);
    if(slot>=512 || (p->space.pt[slot]&PG_PRESENT))return 0;
    void *frame=page_alloc();
    if(!frame)return 0;
    p->sibling_user_stack=frame;
    p->space.pt[slot]=(u64)frame|PG_PRESENT|PG_WRITE|PG_USER|PG_NX;
    p->thread.tid=p->pid*4U;
    p->sibling.tid=p->pid*4U+1U;
    return 1;
}
static int wx_proc_run(wx_managed_process *p){
    if(!p || p->state!=WX_PROC_READY || wx_active)return 0;
    const u64 original_cr3=wx_read_cr3();
    p->state=WX_PROC_RUNNING;
    int success=wx_space_run(&p->space,p->pid,p->exit_code);
    wx_load_cr3(original_cr3);
    p->state=success?WX_PROC_EXITED:WX_PROC_FAILED;
    return success;
}
static int wx_proc_reap(wx_managed_process *p){
    if(!p || p->resident || (p->state!=WX_PROC_EXITED && p->state!=WX_PROC_FAILED) ||
       wx_active)return 0;
#ifdef WINUX_TIMER_PREEMPT_PREVIEW
    /* Never release the CPL3 thread stack/frame while IRQ entry owns it. */
    if(wx_proc_current_irq==p ||
       wx_proc_current_thread==&p->thread ||
       wx_proc_current_thread==&p->sibling)return 0;
    if(p->sibling_user_stack){
        u32 slot=(u32)(((WX_USER_STACK+4096ULL)>>12)&511U);
        if(!(p->space.pt[slot]&PG_PRESENT))return 0;
        p->space.pt[slot]=0;
        if(!page_free(p->sibling_user_stack))return 0;
        p->sibling_user_stack=0;
    }
#endif
    int ok=!obj_close(&p->objects,p->owned_event);
    for(u32 k=0;k<OBJ_SLOTS;k++)if(p->objects.slots[k].kind)ok=0;
    if(!wx_space_destroy(&p->space))ok=0;
    pg_zero(p,sizeof(*p));
    return ok;
}
static int wx_process_manager_test(void){
    if(wx_active || !pg_ready || !wx_prepare_tss())return 0;
    const u64 base_cr3=wx_read_cr3();
    const u32 baseline=wx_proc_free_pages();
    const u32 tables=pg_used;
    int ok=1;
    u32 creations=0;
    for(u32 pass=0;pass<3 && ok;pass++){
        wx_managed_process *jobs[WX_PROC_TRANSIENT_COUNT]={0};
        for(u32 i=0;i<WX_PROC_TRANSIENT_COUNT;i++){
            jobs[i]=wx_proc_spawn(30+i);
            if(!jobs[i]){ok=0;break;}
            creations++;
            for(u32 j=0;j<i;j++)
                if(jobs[j]->pid==jobs[i]->pid ||
                   jobs[j]->space.root==jobs[i]->space.root ||
                   jobs[j]->space.code==jobs[i]->space.code ||
                   jobs[j]->space.stack==jobs[i]->space.stack)ok=0;
        }
        if(ok && wx_proc_spawn(255))ok=0; /* all four slots occupied */
        for(u32 i=0;i<WX_PROC_TRANSIENT_COUNT;i++)if(jobs[i]){
            if(ok && !wx_proc_run(jobs[i]))ok=0;
            if(jobs[i]->state==WX_PROC_READY)jobs[i]->state=WX_PROC_FAILED;
            if(!wx_proc_reap(jobs[i]))ok=0;
        }
        wx_load_cr3(base_cr3);
        if(wx_proc_free_pages()!=baseline || pg_used!=tables)ok=0;
    }
    for(u32 i=0;i<WX_PROC_TRANSIENT_COUNT;i++)
        if(wx_process_table[i].state!=WX_PROC_FREE)ok=0;
    if(ok){
        print("PROC_MANAGER_OK: ");
        number(creations);
        print(" CPL3 process creates/runs/exits/reaps, 4 parallel slots, unique PIDs/CR3, pool fully restored\n");
    }else{
        print("PROC_MANAGER_FAILED: created=");number(creations);
        print(" free_before=");number(baseline);
        print(" free_after=");number(wx_proc_free_pages());print("\n");
    }
    return ok;
}
#endif
