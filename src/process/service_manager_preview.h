/* Two real CPL3 resident processes using the existing paging, PIT, INT80
 * syscall gate and cooperative scheduler. No parallel scheduler or VFS.
 * Single CPU and one active user context at any instant.
 */
#ifndef WINUX_SERVICE_PREVIEW_H
#define WINUX_SERVICE_PREVIEW_H
#define WX_SERVICE_COUNT 2U
#define WX_SERVICE_SAMPLE_OFFSET 0x100U
#define WX_SERVICE_QUANTA_OFFSET 0x108U
#define WX_SERVICE_STOP_OFFSET 0x110U
#define WX_SERVICE_IPC_OFFSET 0x118U
#define WX_SERVICE_POLL_TICKS 10ULL
typedef struct {
    const char *name;
    const char *metric;
    u32 pid,operation;
    wx_managed_process *process;
    u64 last_run_ticks,last_sample;
    u32 generation;
    u64 ipc_value,last_ipc_tick;
    u32 ipc_pending,ipc_sent,ipc_received,ipc_rejected;
    u32 supervised_restarts;
} wx_resident_service;
/* Each entry is a real service specification, not a dummy Task Manager row. */
static wx_resident_service wx_services[WX_SERVICE_COUNT]={
    {.name="clockd",.metric="last_pit_tick",.pid=401,.operation=WX_NATIVE_CLOCK_TICKS},
    {.name="memd",.metric="last_free_pages",.pid=402,.operation=WX_NATIVE_MEMORY_FREE_PAGES}
};
static u32 wx_service_cursor;
static int wx_service_running(const wx_resident_service *s){
    return s && wx_proc_alive(s->process);
}


/* One bounded mailbox in the existing service registry. Not Windows LPC.
 * Kernel, not user arguments, provides the caller's verified service PID. */
static u64 wx_service_ipc_send(u32 sender_pid,u32 receiver_pid,u64 value){
    if(sender_pid!=401 || receiver_pid!=402 || !value)return 0;
    wx_resident_service *sender=&wx_services[0],*receiver=&wx_services[1];
    if(!wx_service_running(sender) || !wx_service_running(receiver))
        return 0;
    if(receiver->ipc_pending){sender->ipc_rejected++;return 0;}
    /* The existing mailbox is the only IPC queue. A sleeping receiver
     * gets the value directly in the saved INT80 return register; it must
     * NOT also remain queued, which would duplicate the message. */
    if(wx_proc_notify_ipc(receiver->process,value)){
        sender->ipc_sent++;
        receiver->ipc_received++;
        return 1;
    }
    receiver->ipc_value=value;receiver->ipc_pending=1;sender->ipc_sent++;
    return 1;
}
static u64 wx_service_ipc_receive(u32 receiver_pid){
    if(receiver_pid!=402)return 0;
    wx_resident_service *receiver=&wx_services[1];
    if(!wx_service_running(receiver) || !receiver->ipc_pending)return 0;
    u64 value=receiver->ipc_value;
    receiver->ipc_value=0;receiver->ipc_pending=0;receiver->ipc_received++;
    return value;
}
/* A single CPL3 IPC receive with bounded wait, backed by the same mailbox.
 * If a message exists, consume immediately. Otherwise atomically mark the
 * current managed recipient BLOCKED while running on its syscall stack.
 * Timeout is delivered by wx_proc_wake_blocked as RAX=0. */
static u64 wx_service_ipc_receive_wait(u32 receiver_pid,int *blocked){
    if(!blocked)return 0;
    *blocked=0;
    if(receiver_pid!=402 || !wx_current_service_pid ||
       !wx_proc_current_irq || !wx_service_running(&wx_services[1]))return 0;
    wx_resident_service *receiver=&wx_services[1];
    if(receiver->ipc_pending)return wx_service_ipc_receive(receiver_pid);
    if(!wx_proc_request_ipc_wait(30))return 0;
    *blocked=1;
    return 0;
}
static int wx_service_ipc_guard_test(void){
    if(wx_service_ipc_send(0,402,123) || wx_service_ipc_send(401,999,456) ||
       wx_service_ipc_receive(401))return 0;
    print("IPC_ACCESS_OK: source PID derived from kernel and invalid targets rejected\n");
    return 1;
}

static void wx_service_compile(wx_resident_service *s){
    u8 *code=(u8*)s->process->space.code;
    pg_zero(code,4096);
    pg_zero(s->process->space.stack,4096);
    u32 n=0;
    /* Loop: perform the service's REAL syscall, store result on its own
     * writable CPL3 page, increment the counter, YIELD, test STOP. */
    code[n++]=0xb8;*(u32*)(code+n)=s->operation;n+=4;
    code[n++]=0xcd;code[n++]=0x80;
    code[n++]=0x48;code[n++]=0xbb;*(u64*)(code+n)=WX_USER_STACK+WX_SERVICE_SAMPLE_OFFSET;n+=8;
    code[n++]=0x48;code[n++]=0x89;code[n++]=0x03;
    if(s->pid==401){
        /* clockd sends its REAL PIT sample to memd's bounded mailbox. */
        code[n++]=0x48;code[n++]=0x89;code[n++]=0xc6; /* mov rsi,rax */
        code[n++]=0xbf;*(u32*)(code+n)=402;n+=4; /* mov edi,402 */
        code[n++]=0xb8;*(u32*)(code+n)=WX_NATIVE_IPC_SEND;n+=4;
        code[n++]=0xcd;code[n++]=0x80;
    }else if(s->pid==402){
        code[n++]=0xb8;*(u32*)(code+n)=WX_NATIVE_IPC_RECEIVE_WAIT;n+=4;
        code[n++]=0xcd;code[n++]=0x80;
        code[n++]=0x48;code[n++]=0xbb;*(u64*)(code+n)=WX_USER_STACK+WX_SERVICE_IPC_OFFSET;n+=8;
        code[n++]=0x48;code[n++]=0x89;code[n++]=0x03;
        code[n++]=0x48;code[n++]=0xbb;*(u64*)(code+n)=WX_USER_STACK+WX_SERVICE_SAMPLE_OFFSET;n+=8;
    }
    code[n++]=0x83;code[n++]=0x43;code[n++]=8;code[n++]=1;
    /* Sleep is a real blocking native syscall. IRQ0 continues to tick
     * while this process is excluded from the ready queue. */
    code[n++]=0xb8;*(u32*)(code+n)=WX_NATIVE_SLEEP_TICKS;n+=4;
    code[n++]=0xbf;*(u32*)(code+n)=WX_SERVICE_POLL_TICKS;n+=4;
    code[n++]=0xcd;code[n++]=0x80;
    code[n++]=0x48;code[n++]=0xbb;*(u64*)(code+n)=WX_USER_STACK+WX_SERVICE_STOP_OFFSET;n+=8;
    code[n++]=0x80;code[n++]=0x3b;code[n++]=0;
    code[n++]=0x74;u32 branch=n++;
    code[branch]=(u8)(0U-n);
    code[n++]=0xb8;*(u32*)(code+n)=WX_NATIVE_EXIT;n+=4;
    code[n++]=0x31;code[n++]=0xff;
    code[n++]=0xcd;code[n++]=0x80;
    code[n++]=0x0f;code[n++]=0x0b;
}
static int wx_service_step(wx_resident_service *s){
    if(!s || !wx_service_running(s) || wx_active || wx_user_resume_target ||
       !s->process->space.root || !wx_sched_kernel_map_ok(&s->process->space))return 0;
    /* Explicit stop/fault diagnostics may resume a blocked thread; ordinary
     * idle scheduling uses wx_proc_pick_ready and never selects BLOCKED. */
    const u64 cr3=wx_read_cr3();
    wx_user_resume_target=&s->process->thread.frame;
    wx_user_yielded=0;wx_timer_preempt_triggered=0;
    wx_exit_requested=0;wx_exit_code=0;
    wx_fault_vector=0;wx_fault_error=0;wx_active=1;
    wx_current_service_pid=s->process->pid;
    wx_proc_current_irq=s->process;
    wx_proc_current_thread=&s->process->thread;
    s->process->state=WX_PROC_RUNNING;
    u64 original_rsp0=wx_tss.rsp0;
    wx_tss.rsp0=((u64)s->process->thread.kernel_stack+
                 sizeof(s->process->thread.kernel_stack)-16)&~15ULL;
    __asm__ volatile("fxsave64 %0":"=m"(wx_timer_kernel_fx)::"memory");
    wx_load_cr3((u64)s->process->space.root);
    if(s->process->thread.started)
        __asm__ volatile("fxrstor64 %0"::"m"(s->process->thread.fxstate):"memory");
    u32 status=s->process->thread.started?wx_resume_user(&s->process->thread.frame):
                          wx_enter_user(WX_USER_CODE,WX_USER_STACK+4096-16);
    __asm__ volatile("fxsave64 %0":"=m"(s->process->thread.fxstate)::"memory");
    __asm__ volatile("fxrstor64 %0"::"m"(wx_timer_kernel_fx):"memory");
    wx_load_cr3(cr3);
    wx_tss.rsp0=original_rsp0;
    wx_active=0;wx_user_resume_target=0;wx_current_service_pid=0;
    wx_proc_current_irq=0;
    wx_proc_current_thread=0;
    s->last_run_ticks=timer_ticks;
    if(s->process->state!=WX_PROC_BLOCKED)
        s->process->next_ready_tick=timer_ticks+WX_SERVICE_POLL_TICKS;
    if(wx_fault_vector || !wx_exit_requested){
        s->process->exit_code=status;s->process->state=WX_PROC_FAILED;return 0;
    }
    if(wx_timer_preempt_triggered && !wx_user_yielded){
        if(status){s->process->state=WX_PROC_FAILED;return 0;}
        s->process->thread.started=1;
        s->process->state=WX_PROC_READY;
        return 1;
    }
    if(wx_user_yielded){
        if(status){s->process->state=WX_PROC_FAILED;return 0;}
        s->process->thread.started=1;
        /* INT80 sleep already moved the owner to BLOCKED. Respect it. */
        if(s->process->state!=WX_PROC_BLOCKED)
            s->process->state=WX_PROC_READY;
        s->last_sample=*(volatile u64*)((u8*)s->process->space.stack+WX_SERVICE_SAMPLE_OFFSET);
        s->process->quanta=*(volatile u32*)((u8*)s->process->space.stack+WX_SERVICE_QUANTA_OFFSET);
        if(s->pid==402)s->last_ipc_tick=*(volatile u64*)((u8*)s->process->space.stack+WX_SERVICE_IPC_OFFSET);
        return 1;
    }
    s->process->exit_code=status;
    s->process->state=(status==0)?WX_PROC_EXITED:WX_PROC_FAILED;
    s->process->thread.started=0;
    return status==0;
}
static int wx_service_start(wx_resident_service *s){
    if(!s || wx_active || wx_user_resume_target)return 0;
    unsigned index=(unsigned)(s-wx_services);
    if(index>=WX_SERVICE_COUNT)return 0;
    s->process=wx_proc_resident(index,s->pid,s->name);
    if(!s->process || wx_service_running(s))return 0;
    /* A resident service reserves six pages from the shared physical
     * allocator once and reuses them across restarts. Unlike temporary
     * Process Manager jobs, this slot remains owned while STOPPED. */
    if(!wx_sched_kernel_map_ok(&s->process->space) || !wx_prepare_tss())return 0;
    wx_service_compile(s);
    pg_zero(&s->process->thread.frame,sizeof(s->process->thread.frame));
    s->process->quanta=0;
    s->process->thread.started=0;
    s->last_run_ticks=timer_ticks;
    s->process->next_ready_tick=timer_ticks+WX_SERVICE_POLL_TICKS;
    s->process->wait_kind=WX_WAIT_NONE;
    s->last_sample=0;s->process->quanta=0;s->process->thread.started=0;s->process->exit_code=0;
    s->ipc_pending=0;s->ipc_value=0;s->last_ipc_tick=0;
    s->ipc_sent=0;s->ipc_received=0;s->ipc_rejected=0;
    s->generation++;s->process->state=WX_PROC_READY;
    return 1;
}
static int wx_service_stop(wx_resident_service *s){
    if(!s || !wx_service_running(s))return 0;
    ((volatile u8*)s->process->space.stack)[WX_SERVICE_STOP_OFFSET]=1;
    for(unsigned turn=0;turn<3 && wx_service_running(s);turn++)
        if(!wx_service_step(s))return 0;
    return s->process->state==WX_PROC_EXITED;
}
/* Fault injection is strictly a kernel diagnostic: it installs a #PF
 * instruction at the SAVED user RIP, resumes the existing CPL3 process
 * and lets the normal user #PF handler terminate that service alone.
 * No kernel-mode exception or arbitrary untrusted SYS is executed. */
static int wx_service_fault_test(void){
    wx_resident_service *s=&wx_services[1];
    if(!s->process->thread.started || !wx_service_running(s) || wx_active)return 0;
    u64 rip=s->process->thread.frame.rip;
    if(rip<WX_USER_CODE || rip>WX_USER_CODE+4096-16)return 0;
    u8 *code=(u8*)s->process->space.code+(u32)(rip-WX_USER_CODE);
    code[0]=0x48;code[1]=0xb8;*(u64*)(code+2)=0x1000ULL;
    code[10]=0x48;code[11]=0x8b;code[12]=0x18; /* mov rbx,[rax] */
    code[13]=0x0f;code[14]=0x0b;
    int finished=wx_service_step(s);
    int isolated=!finished && s->process->state==WX_PROC_FAILED &&
                 wx_fault_vector==14 && wx_current_service_pid==0;
    if(isolated)print("SERVICE_USER_FAULT_OK: memd CPL3 #PF isolated, clockd/kernel still alive\n");
    return isolated;
}
static int wx_service_poll(void){
    if(wx_active)return 0;
    /* Failure supervision is service policy; READY selection belongs solely
     * to the Process Manager, which owns every live PID and CR3. */
    for(unsigned i=0;i<WX_SERVICE_COUNT;i++){
        wx_resident_service *s=&wx_services[i];
        if(s->process && s->process->state==WX_PROC_FAILED &&
           s->supervised_restarts<2 &&
           timer_ticks-s->last_run_ticks>=20){
            if(wx_service_start(s)){
                s->supervised_restarts++;
                return 1;
            }
        }
    }
    wx_managed_process *next=wx_proc_pick_ready();
    if(!next)return 0;
    for(unsigned i=0;i<WX_SERVICE_COUNT;i++){
        wx_resident_service *s=&wx_services[i];
        if(s->process==next)return wx_service_step(s);
    }
    return 0; /* generic non-service executables need their loader policy */
}
/* End-to-end guest test: the existing CPL3 memd must really block
 * on an empty IPC queue, and real clockd sends through INT80 to wake it.
 * A second pass uses the PIT deadline to wake with RAX=0. */
static int wx_service_ipc_block_test(void){
    wx_resident_service *sender=&wx_services[0],*receiver=&wx_services[1];
    if(wx_active || !sender->process || !receiver->process)return 0;
    const u32 free_before=wx_proc_free_pages();
    const u32 wait_before=wx_proc_ipc_waits,wake_before=wx_proc_ipc_wakes;
    const u32 timeout_before=wx_proc_ipc_timeouts;
    int ok=1;
    if(wx_service_running(sender) && !wx_service_stop(sender))return 0;
    if(wx_service_running(receiver) && !wx_service_stop(receiver))return 0;

    /* First round: blocked receiver, then authenticated clockd syscall. */
    if(!wx_service_start(receiver) || !wx_service_step(receiver) ||
       receiver->process->state!=WX_PROC_BLOCKED ||
       receiver->process->wait_kind!=WX_WAIT_IPC ||
       receiver->ipc_pending || wx_proc_ipc_waits!=wait_before+1)
        ok=0;
    u64 queued_deadline=receiver->process->next_ready_tick;
    wx_proc_wake_blocked();
    if(ok && timer_ticks<queued_deadline &&
       receiver->process->state!=WX_PROC_BLOCKED)ok=0;
    if(ok && (!wx_service_start(sender) || !wx_service_step(sender)))ok=0;
    u64 delivered=receiver->process->thread.frame.regs[0];
    if(ok && (!delivered || receiver->process->state!=WX_PROC_READY ||
              receiver->process->wait_kind!=WX_WAIT_NONE ||
              receiver->ipc_pending || wx_proc_ipc_wakes!=wake_before+1 ||
              wx_proc_ipc_timeouts!=timeout_before))ok=0;
    if(ok && (!wx_service_step(receiver) || receiver->last_ipc_tick!=delivered ||
              receiver->process->wait_kind!=WX_WAIT_TIMER))ok=0;
    if(wx_service_running(sender) && !wx_service_stop(sender))ok=0;
    if(wx_service_running(receiver) && !wx_service_stop(receiver))ok=0;

    /* Second round: no sender; the same user syscall must time out after
     * 30 REAL PIT ticks. The message return must be 0 and no stale mail. */
    if(ok && (!wx_service_start(receiver) || !wx_service_step(receiver) ||
              receiver->process->state!=WX_PROC_BLOCKED ||
              receiver->process->wait_kind!=WX_WAIT_IPC))ok=0;
    u64 deadline=receiver->process->next_ready_tick;
    if(ok){
        for(u32 spins=0;spins<128 && timer_ticks<deadline;spins++)
            __asm__ volatile("sti; hlt":::"memory");
        if(timer_ticks<deadline)ok=0;
        wx_proc_wake_blocked();
        if(receiver->process->state!=WX_PROC_READY ||
           receiver->process->wait_kind!=WX_WAIT_NONE ||
           receiver->process->thread.frame.regs[0]!=0 ||
           wx_proc_ipc_timeouts!=timeout_before+1)ok=0;
    }
    if(ok && (!wx_service_step(receiver) || receiver->last_ipc_tick!=0))
        ok=0;
    if(wx_service_running(receiver) && !wx_service_stop(receiver))ok=0;
    if(wx_proc_free_pages()!=free_before)ok=0;
    if(ok)print("IPC_WAIT_WAKE_OK: CPL3 memd blocked, clockd INT80 delivered saved RAX and early READY wake\n");
    else{
        print("IPC_WAIT_WAKE_FAILED sender=");number(sender->process->state);
        print(" receiver=");number(receiver->process->state);
        print(" kind=");number(receiver->process->wait_kind);
        print(" pending=");number(receiver->ipc_pending);
        print(" value=");number(delivered);
        print(" last=");number(receiver->last_ipc_tick);
        print(" waits=");number(wx_proc_ipc_waits-wait_before);
        print(" wakes=");number(wx_proc_ipc_wakes-wake_before);
        print(" timeouts=");number(wx_proc_ipc_timeouts-timeout_before);
        print(" ticks=");number(timer_ticks);
        print(" deadline=");number(queued_deadline);
        print("\n");
    }
    if(ok)print("IPC_WAIT_TIMEOUT_OK: real PIT deadline returns 0, empty mailbox, no page leak\n");
    return ok;
}
static void wx_service_status(void){
    print("SERVICE_LIST_BEGIN\n");
    for(unsigned i=0;i<WX_SERVICE_COUNT;i++){
        wx_resident_service *s=&wx_services[i];
        print("SERVICE ");print(s->name);
        print(" pid=");number(wx_service_running(s)?s->process->pid:0);
        print(" state=");print(wx_service_running(s)?"RUNNING":
                              s->process->state==WX_PROC_FAILED?"FAILED":"STOPPED");
        print(" generation=");number(s->generation);
        print(" quanta=");number(s->process->quanta);
        print(" ");print(s->metric);print("=");number(s->last_sample);
        print(" last_ipc_tick=");number(s->last_ipc_tick);
        print(" sent=");number(s->ipc_sent);
        print(" received=");number(s->ipc_received);
        print(" backpressure=");number(s->ipc_rejected);
        print(" restarts=");number(s->supervised_restarts);
        print("\n");
    }
    print("SERVICE_LIST_END\n");
}
static int wx_service_command(const char *arg){
    if(same(arg,"fault memd")){
        print(wx_service_fault_test()?"SERVICE_FAULT_INJECTED_OK\n":"SERVICE_FAULT_INJECTED_FAILED\n");
        return 1;
    }
    for(unsigned i=0;i<WX_SERVICE_COUNT;i++){
        wx_resident_service *s=&wx_services[i];
        char cmd[32];
        const char *verbs[]={"start ","stop ","restart "};
        for(unsigned v=0;v<3;v++){
            unsigned k=0;
            for(const char *p=verbs[v];*p;p++)cmd[k++]=*p;
            for(const char *p=s->name;*p && k+1<sizeof(cmd);p++)cmd[k++]=*p;
            cmd[k]=0;
            if(!same(arg,cmd))continue;
            int ok=0;
            if(v==0)ok=wx_service_start(s);
            if(v==1)ok=wx_service_stop(s);
            if(v==2)ok=(!wx_service_running(s) || wx_service_stop(s)) &&
                         wx_service_start(s);
            print(ok?(v==0?"SERVICE_START_OK\n":v==1?"SERVICE_STOP_OK\n":"SERVICE_RESTART_OK\n"):
                     (v==0?"SERVICE_START_REJECTED\n":v==1?"SERVICE_STOP_FAILED\n":"SERVICE_RESTART_FAILED\n"));
            return 1;
        }
    }
    print("SERVICE_UNKNOWN\n");
    return 0;
}
static int wx_service_boot(void){
    for(unsigned i=0;i<WX_SERVICE_COUNT;i++)
        if(!wx_service_start(&wx_services[i]))return 0;
    return 1;
}
#endif
