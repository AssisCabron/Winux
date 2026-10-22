/* Two real CPL3 threads of ONE managed process, same CR3+code+handles,
 * separate user stacks, GPR trap frames, FXSTATE, TIDs and TSS.RSP0.
 * This is a bounded test of the existing process's thread lifecycle;
 * not an alternative scheduler or a second process manager. */
#ifndef WINUX_MULTITHREAD_TEST_H
#define WINUX_MULTITHREAD_TEST_H
static int wx_test_thread_quantum(wx_managed_process *p,wx_proc_thread *t,
                                  u64 user_sp,int resume,u32 expected,
                                  int *result_kind){
    if(!p || !t || !p->space.root || wx_active ||
       wx_proc_current_irq || wx_proc_current_thread)return 0;
    const u64 root=wx_read_cr3(),old_rsp0=wx_tss.rsp0;
    wx_proc_current_irq=p;
    wx_proc_current_thread=t;
    wx_user_resume_target=&t->frame;
    wx_user_yielded=0;wx_timer_preempt_triggered=0;
    wx_exit_requested=0;wx_exit_code=0;
    wx_fault_vector=0;wx_fault_error=0;
    wx_active=1;p->state=WX_PROC_RUNNING;
    wx_tss.rsp0=((u64)t->kernel_stack+sizeof(t->kernel_stack)-16)&~15ULL;
    __asm__ volatile("fxsave64 %0":"=m"(wx_timer_kernel_fx)::"memory");
    wx_load_cr3((u64)p->space.root);
    if(resume)__asm__ volatile("fxrstor64 %0"::"m"(t->fxstate):"memory");
    u32 code=resume?wx_resume_user(&t->frame):
                     wx_enter_user(WX_USER_CODE,user_sp);
    __asm__ volatile("fxsave64 %0":"=m"(t->fxstate)::"memory");
    __asm__ volatile("fxrstor64 %0"::"m"(wx_timer_kernel_fx):"memory");
    wx_load_cr3(root);
    wx_tss.rsp0=old_rsp0;
    wx_active=0;wx_user_resume_target=0;
    wx_proc_current_irq=0;wx_proc_current_thread=0;
    if(wx_fault_vector || !wx_exit_requested)return 0;
    if(wx_timer_preempt_triggered && !wx_user_yielded){
        t->started=1;t->interrupt_count++;*result_kind=3;
        p->state=WX_PROC_READY;return code==0;
    }
    if(wx_user_yielded){
        if(code)return 0;
        t->started=1;*result_kind=1;p->state=WX_PROC_READY;
        if(t->frame.rsp!=user_sp || t->frame.rip<=WX_USER_CODE)return 0;
        return 1;
    }
    if(code!=expected)return 0;
    t->resume_count++;
    p->state=WX_PROC_READY;
    *result_kind=2;
    return 1;
}
static int wx_multithread_test(void){
    if(wx_active || !wx_prepare_tss() || wx_proc_current_irq)return 0;
    const u32 base_free=wx_proc_free_pages();
    const u32 base_tables=pg_used;
    int ok=1;
    u32 total_threads=0;
    for(unsigned pass=0;pass<3 && ok;pass++){
        wx_managed_process *p=wx_proc_spawn(0);
        if(!p)return 0;
        if(!wx_proc_add_sibling(p))ok=0;
        u64 sp0=WX_USER_STACK+4096-16;
        u64 sp1=WX_USER_STACK+8192-16;
        if(ok){
            u8 *a=(u8*)p->space.stack,*b=(u8*)p->sibling_user_stack;
            *(u32*)(a+4096-16)=71U;*(u32*)(b+4096-16)=72U;
            *(u32*)(a+4096-8)=0x1f80U;
            *(u32*)(b+4096-8)=0x3f80U;
            /* Shared immutable executable page: each thread samples its OWN
             * stack, writes its own MXCSR, yields, stores MXCSR after resume
             * then exits with the value stored on its own user stack. */
            u8 *ins=(u8*)p->space.code;
            pg_zero(ins,4096);
            unsigned n=0;
            ins[n++]=0x0f;ins[n++]=0xae;ins[n++]=0x54;ins[n++]=0x24;ins[n++]=8;
            ins[n++]=0xb8;*(u32*)(ins+n)=WX_NATIVE_YIELD;n+=4;
            ins[n++]=0xcd;ins[n++]=0x80;
            ins[n++]=0x0f;ins[n++]=0xae;ins[n++]=0x5c;ins[n++]=0x24;ins[n++]=12;
            ins[n++]=0xb8;*(u32*)(ins+n)=WX_NATIVE_EXIT;n+=4;
            ins[n++]=0x8b;ins[n++]=0x3c;ins[n++]=0x24;
            ins[n++]=0xcd;ins[n++]=0x80;
            ins[n++]=0x0f;ins[n++]=0x0b;
            if(p->thread.tid==p->sibling.tid ||
               p->thread.tid==0 || p->space.root==0 ||
               p->space.stack==p->sibling_user_stack)ok=0;
        }
        wx_proc_thread *threads[2]={&p->thread,&p->sibling};
        const u64 sps[2]={sp0,sp1};
        const u32 exits[2]={71,72};
        int finished[2]={0,0},first_yield[2]={0,0};
        for(unsigned turn=0;turn<12 && ok;turn++){
            unsigned i=turn&1U;
            if(finished[i])continue;
            int kind=0;
            if(!wx_test_thread_quantum(p,threads[i],sps[i],threads[i]->started,
                                       exits[i],&kind)){ok=0;break;}
            if(kind==1)first_yield[i]++;
            if(kind==2)finished[i]=1;
            if(finished[0] && finished[1])break;
        }
        if(!finished[0] || !finished[1] ||
           first_yield[0]!=1 || first_yield[1]!=1 ||
           *(u32*)((u8*)p->space.stack+4096-4)!=0x1f80U ||
           *(u32*)((u8*)p->sibling_user_stack+4096-4)!=0x3f80U)
            ok=0;
        if(p->state!=WX_PROC_READY)ok=0;
        p->state=ok?WX_PROC_EXITED:WX_PROC_FAILED;
        if(!wx_proc_reap(p) || wx_proc_free_pages()!=base_free ||
           pg_used!=base_tables)ok=0;
        if(ok)total_threads+=2;
    }
    if(ok){
        print("MULTITHREAD_CPL3_OK: ");
        number(total_threads);
        print(" threads in 3 shared-CR3 processes, unique TIDs/user stacks/RSP0, independent MXCSR, YIELD/EXIT and full reaping\n");
    }else print("MULTITHREAD_CPL3_FAILED\n");
    return ok;
}
#endif
