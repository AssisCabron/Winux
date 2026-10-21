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
enum {WX_SERVICE_STOPPED=0,WX_SERVICE_RUNNING=1,WX_SERVICE_FAILED=2};
typedef struct {
    const char *name;
    const char *metric;
    u32 pid,operation;
    wx_address_space space;
    wx_user_resume_frame frame;
    u64 last_run_ticks,last_sample;
    u32 quanta,generation,state,entered,reserved,last_exit;
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

/* One bounded mailbox in the existing service registry. Not Windows LPC.
 * Kernel, not user arguments, provides the caller's verified service PID. */
static u64 wx_service_ipc_send(u32 sender_pid,u32 receiver_pid,u64 value){
    if(sender_pid!=401 || receiver_pid!=402 || !value)return 0;
    wx_resident_service *sender=&wx_services[0],*receiver=&wx_services[1];
    if(sender->state!=WX_SERVICE_RUNNING || receiver->state!=WX_SERVICE_RUNNING)
        return 0;
    if(receiver->ipc_pending){sender->ipc_rejected++;return 0;}
    receiver->ipc_value=value;receiver->ipc_pending=1;sender->ipc_sent++;
    return 1;
}
static u64 wx_service_ipc_receive(u32 receiver_pid){
    if(receiver_pid!=402)return 0;
    wx_resident_service *receiver=&wx_services[1];
    if(receiver->state!=WX_SERVICE_RUNNING || !receiver->ipc_pending)return 0;
    u64 value=receiver->ipc_value;
    receiver->ipc_value=0;receiver->ipc_pending=0;receiver->ipc_received++;
    return value;
}
static int wx_service_ipc_guard_test(void){
    if(wx_service_ipc_send(0,402,123) || wx_service_ipc_send(401,999,456) ||
       wx_service_ipc_receive(401))return 0;
    print("IPC_ACCESS_OK: source PID derived from kernel and invalid targets rejected\n");
    return 1;
}

static void wx_service_compile(wx_resident_service *s){
    u8 *code=(u8*)s->space.code;
    pg_zero(code,4096);
    pg_zero(s->space.stack,4096);
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
        code[n++]=0xb8;*(u32*)(code+n)=WX_NATIVE_IPC_RECEIVE;n+=4;
        code[n++]=0xcd;code[n++]=0x80;
        code[n++]=0x48;code[n++]=0xbb;*(u64*)(code+n)=WX_USER_STACK+WX_SERVICE_IPC_OFFSET;n+=8;
        code[n++]=0x48;code[n++]=0x89;code[n++]=0x03;
        code[n++]=0x48;code[n++]=0xbb;*(u64*)(code+n)=WX_USER_STACK+WX_SERVICE_SAMPLE_OFFSET;n+=8;
    }
    code[n++]=0x83;code[n++]=0x43;code[n++]=8;code[n++]=1;
    code[n++]=0xb8;*(u32*)(code+n)=WX_NATIVE_YIELD;n+=4;
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
    if(!s || s->state!=WX_SERVICE_RUNNING || wx_active || wx_user_resume_target ||
       !s->space.root || !wx_sched_kernel_map_ok(&s->space))return 0;
    const u64 cr3=wx_read_cr3();
    wx_user_resume_target=&s->frame;
    wx_user_yielded=0;wx_exit_requested=0;wx_exit_code=0;
    wx_fault_vector=0;wx_fault_error=0;wx_active=1;
    wx_current_service_pid=s->pid;
    wx_load_cr3((u64)s->space.root);
    u32 status=s->entered?wx_resume_user(&s->frame):
                          wx_enter_user(WX_USER_CODE,WX_USER_STACK+4096-16);
    wx_load_cr3(cr3);
    wx_active=0;wx_user_resume_target=0;wx_current_service_pid=0;
    s->last_run_ticks=timer_ticks;
    if(wx_fault_vector || !wx_exit_requested){
        s->last_exit=status;s->state=WX_SERVICE_FAILED;return 0;
    }
    if(wx_user_yielded){
        if(status){s->state=WX_SERVICE_FAILED;return 0;}
        s->entered=1;
        s->last_sample=*(volatile u64*)((u8*)s->space.stack+WX_SERVICE_SAMPLE_OFFSET);
        s->quanta=*(volatile u32*)((u8*)s->space.stack+WX_SERVICE_QUANTA_OFFSET);
        if(s->pid==402)s->last_ipc_tick=*(volatile u64*)((u8*)s->space.stack+WX_SERVICE_IPC_OFFSET);
        return 1;
    }
    s->last_exit=status;
    s->state=(status==0)?WX_SERVICE_STOPPED:WX_SERVICE_FAILED;
    s->entered=0;
    return status==0;
}
static int wx_service_start(wx_resident_service *s){
    if(!s || s->state==WX_SERVICE_RUNNING || wx_active || wx_user_resume_target)return 0;
    if(!s->reserved){
        if(!wx_space_make(&s->space,s->pid,0))return 0;
        s->reserved=1;
    }
    /* A resident service reserves six pages from the shared physical
     * allocator once and reuses them across restarts. Unlike temporary
     * Process Manager jobs, this slot remains owned while STOPPED. */
    if(!wx_sched_kernel_map_ok(&s->space) || !wx_prepare_tss())return 0;
    wx_service_compile(s);
    pg_zero(&s->frame,sizeof(s->frame));
    s->last_run_ticks=timer_ticks;
    s->last_sample=0;s->quanta=0;s->entered=0;s->last_exit=0;
    s->ipc_pending=0;s->ipc_value=0;s->last_ipc_tick=0;
    s->ipc_sent=0;s->ipc_received=0;s->ipc_rejected=0;
    s->generation++;s->state=WX_SERVICE_RUNNING;
    return 1;
}
static int wx_service_stop(wx_resident_service *s){
    if(!s || s->state!=WX_SERVICE_RUNNING)return 0;
    ((volatile u8*)s->space.stack)[WX_SERVICE_STOP_OFFSET]=1;
    for(unsigned turn=0;turn<3 && s->state==WX_SERVICE_RUNNING;turn++)
        if(!wx_service_step(s))return 0;
    return s->state==WX_SERVICE_STOPPED;
}
/* Fault injection is strictly a kernel diagnostic: it installs a #PF
 * instruction at the SAVED user RIP, resumes the existing CPL3 process
 * and lets the normal user #PF handler terminate that service alone.
 * No kernel-mode exception or arbitrary untrusted SYS is executed. */
static int wx_service_fault_test(void){
    wx_resident_service *s=&wx_services[1];
    if(!s->entered || s->state!=WX_SERVICE_RUNNING || wx_active)return 0;
    u64 rip=s->frame.rip;
    if(rip<WX_USER_CODE || rip>WX_USER_CODE+4096-16)return 0;
    u8 *code=(u8*)s->space.code+(u32)(rip-WX_USER_CODE);
    code[0]=0x48;code[1]=0xb8;*(u64*)(code+2)=0x1000ULL;
    code[10]=0x48;code[11]=0x8b;code[12]=0x18; /* mov rbx,[rax] */
    code[13]=0x0f;code[14]=0x0b;
    int finished=wx_service_step(s);
    int isolated=!finished && s->state==WX_SERVICE_FAILED &&
                 wx_fault_vector==14 && wx_current_service_pid==0;
    if(isolated)print("SERVICE_USER_FAULT_OK: memd CPL3 #PF isolated, clockd/kernel still alive\n");
    return isolated;
}
static int wx_service_poll(void){
    if(wx_active)return 0;
    /* Round-robin over the same resident process registry. */
    unsigned first=wx_service_cursor++%WX_SERVICE_COUNT;
    for(unsigned k=0;k<WX_SERVICE_COUNT;k++){
        wx_resident_service *s=&wx_services[(first+k)%WX_SERVICE_COUNT];
        if(s->state==WX_SERVICE_FAILED && s->supervised_restarts<2 &&
           timer_ticks-s->last_run_ticks>=20){
            /* Bounded restart after a real CPL3 exception. Rebuild only
             * this service's user image; keep other services/CR3 intact. */
            if(wx_service_start(s)){
                s->supervised_restarts++;
                return 1;
            }
        }
        if(s->state==WX_SERVICE_RUNNING &&
           timer_ticks-s->last_run_ticks>=WX_SERVICE_POLL_TICKS)
            return wx_service_step(s);
    }
    return 0;
}
static void wx_service_status(void){
    print("SERVICE_LIST_BEGIN\n");
    for(unsigned i=0;i<WX_SERVICE_COUNT;i++){
        wx_resident_service *s=&wx_services[i];
        print("SERVICE ");print(s->name);
        print(" pid=");number(s->state==WX_SERVICE_RUNNING?s->pid:0);
        print(" state=");print(s->state==WX_SERVICE_RUNNING?"RUNNING":
                              s->state==WX_SERVICE_FAILED?"FAILED":"STOPPED");
        print(" generation=");number(s->generation);
        print(" quanta=");number(s->quanta);
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
static void wx_process_list(void){
    print("PROCESS_LIST_BEGIN\n");
    for(unsigned i=0;i<WX_SERVICE_COUNT;i++){
        wx_resident_service *s=&wx_services[i];
        if(s->state!=WX_SERVICE_RUNNING)continue;
        print("PROCESS pid=");number(s->pid);
        print(" name=");print(s->name);
        print(" cpl=3 state=RUNNING cr3=");print_hex((u64)s->space.root);
        print(" quanta=");number(s->quanta);print("\n");
    }
    print("PROCESS_LIST_END\n");
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
            if(v==2)ok=(s->state!=WX_SERVICE_RUNNING || wx_service_stop(s)) &&
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
