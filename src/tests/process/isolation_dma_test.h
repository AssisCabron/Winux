/* Real CPL3 CPU isolation and DMA aperture regressions.
 * No fabricated mappings: test actual CR3 and real hardware #PF when
 * a process reads another process's low physical frame or DMA pool.
 * All allocations and process handles are reclaimed after each test.
 */
#ifndef WINUX_MEMORY_DMA_ISOLATION_TEST_H
#define WINUX_MEMORY_DMA_ISOLATION_TEST_H
/* Exercise the real x86 page-fault protection in user mode. Mode 0
 * reads supervisor-only physical memory, 1 writes RX user code, 2 tries to
 * EXECUTE NX user stack. Only the instruction varies; the CPL3 trap/reap
 * path is shared, avoiding independent implementations for each test. */
static int wx_isolation_fault_access(wx_managed_process *p,u64 target,
                                     u32 mode,u64 required_error){
    if(!p || !p->space.root || wx_active || mode>2)return 0;
    u8 *code=(u8*)p->space.code;
    pg_zero(code,64);
    u32 n=0;
    code[n++]=0x48;code[n++]=0xb8;
    *(u64*)(code+n)=target;n+=8; /* movabs rax,target */
    if(mode==0){code[n++]=0x48;code[n++]=0x8b;code[n++]=0x18;} /* mov rbx,[rax] */
    else if(mode==1){code[n++]=0x48;code[n++]=0x89;code[n++]=0x18;} /* mov [rax],rbx */
    else {code[n++]=0xff;code[n++]=0xe0;} /* jmp rax (stack NX) */
    code[n++]=0x0f;code[n++]=0x0b; /* must never execute */
    u64 old_cr3=wx_read_cr3(),old_rsp0=wx_tss.rsp0;
    wx_tss.rsp0=((u64)p->thread.kernel_stack+
                 sizeof(p->thread.kernel_stack)-16)&~15ULL;
    wx_proc_current_irq=p;wx_proc_current_thread=&p->thread;
    wx_active=1;wx_exit_requested=0;wx_fault_vector=0;wx_fault_error=0;
    wx_load_cr3((u64)p->space.root);
    u32 result=wx_enter_user(WX_USER_CODE,WX_USER_STACK+4096-16);
    wx_load_cr3(old_cr3);
    wx_tss.rsp0=old_rsp0;
    wx_active=0;wx_proc_current_irq=0;wx_proc_current_thread=0;
    return result==98 && wx_fault_vector==14 &&
           (wx_fault_error&required_error)==required_error;
}
static int wx_isolation_dma_test(void){
    if(wx_active || !pg_ready || !wx_prepare_tss())return 0;
    const u32 free_before=wx_proc_free_pages(),tables=pg_used;
    wx_managed_process *first=wx_proc_spawn(11);
    wx_managed_process *second=first?wx_proc_spawn(22):0;
    u32 stage=0;
    int ok=first && second && first->pid!=second->pid &&
           first->space.root!=second->space.root &&
           first->space.code!=second->space.code &&
           first->space.stack!=second->space.stack;
    if(!ok)stage=1;
    u64 original_cr3=wx_read_cr3();
    if(ok){
        for(unsigned index=0;index<2;index++){
            wx_managed_process *p=index?second:first;
            wx_managed_process *other=index?first:second;
            wx_load_cr3((u64)p->space.root);
            const u64 code_pte=p->space.pt[(WX_USER_CODE>>12)&511ULL];
            const u64 stack_pte=p->space.pt[(WX_USER_STACK>>12)&511ULL];
            if((code_pte&(PG_PRESENT|PG_USER))!=(PG_PRESENT|PG_USER) ||
               (code_pte&PG_WRITE) || (code_pte&PG_NX) ||
               (stack_pte&(PG_PRESENT|PG_WRITE|PG_USER|PG_NX))!=
                (PG_PRESENT|PG_WRITE|PG_USER|PG_NX) ||
               !wx_user_readable(WX_USER_CODE+0x100,4) ||
               !wx_user_readable(WX_USER_STACK+0x100,4) ||
               wx_user_readable(WX_USER_STACK+4095,2) ||
               wx_user_readable((u64)wx_dma_pool,16) ||
               wx_user_readable((u64)other->space.code,16) ||
               wx_user_readable(~0ULL,16)){ok=0;stage=10+index;}
            wx_load_cr3(original_cr3);
        }
    }
    if(ok && !wx_isolation_fault_access(first,(u64)second->space.stack,0,0x5)){ok=0;stage=20;}
    if(ok && !wx_isolation_fault_access(second,(u64)wx_dma_pool,0,0x5)){ok=0;stage=21;}
    if(ok && !wx_isolation_fault_access(first,WX_USER_CODE+0x100,1,0x7)){ok=0;stage=22;}
    if(ok && !wx_isolation_fault_access(second,WX_USER_STACK+0x100,2,0x15)){ok=0;stage=23;}
    wx_load_cr3(original_cr3);
    if(first){
        first->state=WX_PROC_FAILED;
        if(!wx_proc_reap(first)){ok=0;stage=30;}
    }
    if(second){
        second->state=WX_PROC_FAILED;
        if(!wx_proc_reap(second)){ok=0;stage=31;}
    }
    if(wx_proc_free_pages()!=free_before || pg_used!=tables){ok=0;stage=40;}
    if(ok){
        print("CPU_ISOLATION_OK: two CR3 roots, RX vs NX/RW, user buffers bounded, cross-process and DMA pool reads raised CPL3 #PF\n");
        print("ISOLATION_REAP_OK: user faults contained, physical frames and page tables reclaimed\n");
        print("CPU_WX_NX_OK: CPL3 write to RX code and execution from RW/NX stack rejected by actual page faults\n");
    }else{
        print("CPU_ISOLATION_FAILED stage=");number(stage);
        print(" vector=");number(wx_fault_vector);
        print(" error=");print_hex(wx_fault_error);
        print(" free_before=");number(free_before);
        print(" free_after=");number(wx_proc_free_pages());
        print("\n");
    }
    return ok;
}
#endif
