/* One real resident CPL3 service, scheduled by the existing idle loop.
 * Reuses native INT80, wx_address_space, wx_user_resume_frame and PID/CR3.
 * Does not duplicate the kernel scheduler, represent fake Windows services,
 * use external DLLs or claim timer IRQ preemption.
 */
#ifndef WINUX_SERVICE_PREVIEW_H
#define WINUX_SERVICE_PREVIEW_H
#define WX_SERVICE_PID 401U
#define WX_SERVICE_SAMPLE_OFFSET 0x100U
#define WX_SERVICE_COUNT_OFFSET 0x108U
#define WX_SERVICE_STOP_OFFSET 0x110U
#define WX_SERVICE_POLL_TICKS 10ULL
enum {WX_SERVICE_STOPPED=0,WX_SERVICE_RUNNING=1,WX_SERVICE_FAILED=2};
typedef struct {
    wx_address_space space;
    wx_user_resume_frame frame;
    u64 last_run_ticks;
    u64 last_sample_ticks;
    u32 runs;
    u32 generation;
    u32 state;
    u32 entered;
    u32 reserved;
    u32 last_exit;
} wx_resident_service;
static wx_resident_service wx_clock_service;

static void wx_service_build_program(wx_address_space *space){
    u8 *code=(u8*)space->code;
    pg_zero(code,4096);
    pg_zero(space->stack,4096);
    u32 n=0;
    const u64 sample_va=WX_USER_STACK+WX_SERVICE_SAMPLE_OFFSET;
    const u64 stop_va=WX_USER_STACK+WX_SERVICE_STOP_OFFSET;
    /* loop: native clock-ticks syscall -> sample -> count -> native yield */
    code[n++]=0xb8;*(u32*)(code+n)=WX_NATIVE_CLOCK_TICKS;n+=4;
    code[n++]=0xcd;code[n++]=0x80;
    code[n++]=0x48;code[n++]=0xbb;*(u64*)(code+n)=sample_va;n+=8;
    code[n++]=0x48;code[n++]=0x89;code[n++]=0x03;
    code[n++]=0x83;code[n++]=0x43;code[n++]=8;code[n++]=1;
    code[n++]=0xb8;*(u32*)(code+n)=WX_NATIVE_YIELD;n+=4;
    code[n++]=0xcd;code[n++]=0x80;
    code[n++]=0x48;code[n++]=0xbb;*(u64*)(code+n)=stop_va;n+=8;
    code[n++]=0x80;code[n++]=0x3b;code[n++]=0;
    code[n++]=0x74;
    u32 branch=n++;
    if(n>127U)return;
    code[branch]=(u8)(0U-n); /* branch back to first clock syscall */
    code[n++]=0xb8;*(u32*)(code+n)=WX_NATIVE_EXIT;n+=4;
    code[n++]=0x31;code[n++]=0xff; /* xor edi,edi */
    code[n++]=0xcd;code[n++]=0x80;
    code[n++]=0x0f;code[n++]=0x0b;
}
static int wx_service_step(void){
    wx_resident_service *s=&wx_clock_service;
    if(s->state!=WX_SERVICE_RUNNING || wx_active || wx_user_resume_target ||
       !s->space.root)return 0;
    const u64 old_cr3=wx_read_cr3();
    if(!wx_sched_kernel_map_ok(&s->space))return 0;
    wx_user_resume_target=&s->frame;
    wx_user_yielded=0;
    wx_exit_requested=0;wx_exit_code=0;
    wx_fault_vector=0;wx_fault_error=0;wx_active=1;
    wx_load_cr3((u64)s->space.root);
    u32 status=s->entered?wx_resume_user(&s->frame):
                          wx_enter_user(WX_USER_CODE,WX_USER_STACK+4096-16);
    wx_load_cr3(old_cr3);
    wx_active=0;wx_user_resume_target=0;
    s->last_run_ticks=timer_ticks;
    if(wx_fault_vector || !wx_exit_requested){
        s->state=WX_SERVICE_FAILED;s->last_exit=status;return 0;
    }
    if(wx_user_yielded){
        if(status!=0){s->state=WX_SERVICE_FAILED;return 0;}
        s->entered=1;
        s->last_sample_ticks=*(volatile u64*)((u8*)s->space.stack+WX_SERVICE_SAMPLE_OFFSET);
        s->runs=*(volatile u32*)((u8*)s->space.stack+WX_SERVICE_COUNT_OFFSET);
        return 1;
    }
    s->last_exit=status;
    s->state=(status==0)?WX_SERVICE_STOPPED:WX_SERVICE_FAILED;
    s->entered=0;
    return status==0;
}
static int wx_service_start(void){
    wx_resident_service *s=&wx_clock_service;
    if(s->state==WX_SERVICE_RUNNING)return 0;
    if(wx_active || wx_user_resume_target)return 0;
    if(!s->reserved){
        if(!wx_space_make(&s->space,WX_SERVICE_PID,0))return 0;
        s->reserved=1;
    }
    /* This slot is reserved once; restart does not consume page-table or
     * physical-frame pool entries. The reserved pages are not freed until
     * kernel shutdown because pg_new() currently uses a monotonic pool. */
    if(!wx_sched_kernel_map_ok(&s->space) || !wx_prepare_tss())return 0;
    wx_service_build_program(&s->space);
    pg_zero(&s->frame,sizeof(s->frame));
    s->last_run_ticks=timer_ticks;
    s->last_sample_ticks=0;
    s->runs=0;
    s->entered=0;
    s->last_exit=0;
    s->generation++;
    s->state=WX_SERVICE_RUNNING;
    return 1;
}
static int wx_service_stop(void){
    wx_resident_service *s=&wx_clock_service;
    if(s->state!=WX_SERVICE_RUNNING)return 0;
    ((volatile u8*)s->space.stack)[WX_SERVICE_STOP_OFFSET]=1;
    /* If the service has not yet run, its first quantum stops at YIELD.
     * The second quantum reaches native EXIT. No kernel stack is freed. */
    for(unsigned turn=0;turn<3 && s->state==WX_SERVICE_RUNNING;turn++)
        if(!wx_service_step())return 0;
    return s->state==WX_SERVICE_STOPPED;
}
static int wx_service_poll(void){
    wx_resident_service *s=&wx_clock_service;
    if(s->state!=WX_SERVICE_RUNNING || wx_active ||
       timer_ticks-s->last_run_ticks<WX_SERVICE_POLL_TICKS)return 0;
    return wx_service_step();
}
static void wx_service_status(void){
    wx_resident_service *s=&wx_clock_service;
    print("SERVICE_LIST_BEGIN\n");
    print("SERVICE clockd pid=");number(s->state?WX_SERVICE_PID:0);
    print(" state=");
    print(s->state==WX_SERVICE_RUNNING?"RUNNING":
          s->state==WX_SERVICE_FAILED?"FAILED":"STOPPED");
    print(" generation=");number(s->generation);
    print(" quanta=");number(s->runs);
    print(" last_pit_tick=");number(s->last_sample_ticks);
    print("\nSERVICE_LIST_END\n");
}
static void wx_process_list(void){
    wx_resident_service *s=&wx_clock_service;
    print("PROCESS_LIST_BEGIN\n");
    /* The shell is currently privileged execution, NOT a CPL3 process;
     * do not pretend it has a PID. */
    if(s->state==WX_SERVICE_RUNNING){
        print("PROCESS pid=");number(WX_SERVICE_PID);
        print(" name=clockd cpl=3 state=RUNNING cr3=");
        print_hex((u64)s->space.root);
        print(" quanta=");number(s->runs);print("\n");
    }
    print("PROCESS_LIST_END\n");
}
#endif
