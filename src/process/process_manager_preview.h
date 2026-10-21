/* Minimal native process manager with REAL CPL3 execution and full reaping.
 * Reuses the existing address-space allocator, NT object table and INT80
 * loader fixture. A process slot is NOT a fake Task Manager row.
 * Single-core synchronous initial process executions, not generic PE loading.
 */
#ifndef WINUX_PROCESS_MANAGER_PREVIEW_H
#define WINUX_PROCESS_MANAGER_PREVIEW_H
#define WX_PROC_CAPACITY 4U
enum {WX_PROC_FREE=0,WX_PROC_READY=1,WX_PROC_RUNNING=2,
      WX_PROC_EXITED=3,WX_PROC_FAILED=4};
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
} wx_proc_thread;
_Static_assert(__builtin_offsetof(wx_proc_thread,fxstate)%16==0,
               "FXSAVE state must be 16-byte aligned");
#endif
typedef struct {
    wx_address_space space;
    obj_table objects;
    u64 owned_event;
    u32 pid,state,exit_code;
#ifdef WINUX_TIMER_PREEMPT_PREVIEW
    wx_proc_thread thread;
#endif
} wx_managed_process;
static wx_managed_process wx_process_table[WX_PROC_CAPACITY];
#ifdef WINUX_TIMER_PREEMPT_PREVIEW
static wx_managed_process *wx_proc_current_irq;
#endif
static u32 wx_next_process_id=600;

static u32 wx_proc_free_pages(void){
    u32 available=0;
    for(u32 i=0;i<PAGE_POOL_COUNT;i++)if(!page_used[i])available++;
    return available;
}
static wx_managed_process *wx_proc_spawn(u32 exit_code){
    if(wx_active || wx_user_resume_target)return 0;
    for(u32 i=0;i<WX_PROC_CAPACITY;i++){
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
    if(!p || (p->state!=WX_PROC_EXITED && p->state!=WX_PROC_FAILED) ||
       wx_active)return 0;
#ifdef WINUX_TIMER_PREEMPT_PREVIEW
    /* Never release the CPL3 thread stack/frame while IRQ entry owns it. */
    if(wx_proc_current_irq==p)return 0;
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
        wx_managed_process *jobs[WX_PROC_CAPACITY]={0};
        for(u32 i=0;i<WX_PROC_CAPACITY;i++){
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
        for(u32 i=0;i<WX_PROC_CAPACITY;i++)if(jobs[i]){
            if(ok && !wx_proc_run(jobs[i]))ok=0;
            if(jobs[i]->state==WX_PROC_READY)jobs[i]->state=WX_PROC_FAILED;
            if(!wx_proc_reap(jobs[i]))ok=0;
        }
        wx_load_cr3(base_cr3);
        if(wx_proc_free_pages()!=baseline || pg_used!=tables)ok=0;
    }
    for(u32 i=0;i<WX_PROC_CAPACITY;i++)
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
