/* Per-address-space CR3 preview using existing pg_new(), page_alloc(), Ring3 entry.
 * Two independent user address spaces, one CPU and sequential dispatch.
 * This tests real CR3 isolation, NOT an SMP/process scheduler or Windows task. */
#ifndef WINUX_PROC_SPACE_PREVIEW_H
#define WINUX_PROC_SPACE_PREVIEW_H
#define WX_SPACE_COUNT 2
typedef struct {
    u64 *root;
    u64 *pdpt;
    u64 *pd;
    u64 *pt;
    void *code;
    void *stack;
    u32 pid;
    u32 exit_code;
} wx_address_space;
static wx_address_space wx_spaces[WX_SPACE_COUNT];

static int wx_space_make(wx_address_space *p,u32 pid,u32 exit_code) {
    if(!p || !pg_ready || p->root || p->code || p->stack)return 0;
    /* User page tables have explicit ownership. pg_new() is used for
     * permanent kernel mappings, not dynamically created processes. */
    p->root=page_alloc();p->pdpt=page_alloc();
    p->pd=page_alloc();p->pt=page_alloc();
    p->code=page_alloc();p->stack=page_alloc();
    if(!p->root||!p->pdpt||!p->pd||!p->pt||!p->code||!p->stack){
        if(p->code)page_free(p->code);
        if(p->stack)page_free(p->stack);
        if(p->pt)page_free(p->pt);
        if(p->pd)page_free(p->pd);
        if(p->pdpt)page_free(p->pdpt);
        if(p->root)page_free(p->root);
        pg_zero(p,sizeof(*p));
        return 0;
    }
    p->pid=pid;p->exit_code=exit_code;
    const u64 *kernel_pdpt=(const u64*)(pg_root[0]&PG_MASK);
    /* Identity ranges remain supervisor-only; high user region gets its
     * own PDPT/PD/PT. Sharing the low 4GiB cannot expose it to CPL3 because
     * the copied kernel PDPT entries do not set the U/S bit. */
    for(unsigned i=0;i<4;i++)p->pdpt[i]=kernel_pdpt[i];
    p->root[0]=(u64)p->pdpt|PG_PRESENT|PG_WRITE|PG_USER;
    const u32 pdpt_index=(u32)((WX_USER_CODE>>30)&511);
    const u32 pd_index=(u32)((WX_USER_CODE>>21)&511);
    const u32 code_pt=(u32)((WX_USER_CODE>>12)&511);
    const u32 stack_pt=(u32)((WX_USER_STACK>>12)&511);
    if(pdpt_index<4 || code_pt==stack_pt)return 0;
    p->pdpt[pdpt_index]=(u64)p->pd|PG_PRESENT|PG_WRITE|PG_USER;
    p->pd[pd_index]=(u64)p->pt|PG_PRESENT|PG_WRITE|PG_USER;
    p->pt[code_pt]=(u64)p->code|PG_PRESENT|PG_USER;
    p->pt[stack_pt]=(u64)p->stack|PG_PRESENT|PG_WRITE|PG_USER|PG_NX;

    /* Trusted miniature CPL3 program. Under the scheduler preview, it
     * suspends with native YIELD and must resume *after* the INT 80h to EXIT. */
    u8 *code=(u8*)p->code;
    unsigned n=0;
#ifdef WINUX_USER_SCHED_PREVIEW
    code[n++]=0xb8;*(u32*)(code+n)=WX_NATIVE_YIELD;n+=4;
    code[n++]=0xcd;code[n++]=0x80;
#endif
    code[n++]=0xb8;*(u32*)(code+n)=WX_NATIVE_EXIT;n+=4; /* mov eax,0 */
    code[n++]=0xbf;*(u32*)(code+n)=exit_code;n+=4;      /* mov edi,code */
    code[n++]=0xcd;code[n++]=0x80;                      /* int 80h */
    code[n++]=0x0f;code[n++]=0x0b;                      /* fail stop */
    *(u32*)(code+0x100)=pid;
    return 1;
}
static inline u64 wx_read_cr3(void){u64 v;__asm__ volatile("mov %%cr3,%0":"=r"(v));return v;}
static inline void wx_load_cr3(u64 value){__asm__ volatile("mov %0,%%cr3"::"r"(value):"memory");}
/* Fully reclaim a stopped CPL3 process: two user frames and four user
 * page tables. MUST run on a supervisor stack after switching away
 * from this process's root; never reclaim the currently active CR3. */
static int wx_space_destroy(wx_address_space *p){
    if(!p || !p->root || wx_read_cr3()==(u64)p->root || wx_active)return 0;
    int ok=1;
    if(p->code && !page_free(p->code))ok=0;
    if(p->stack && !page_free(p->stack))ok=0;
    if(p->pt && !page_free(p->pt))ok=0;
    if(p->pd && !page_free(p->pd))ok=0;
    if(p->pdpt && !page_free(p->pdpt))ok=0;
    if(p->root && !page_free(p->root))ok=0;
    pg_zero(p,sizeof(*p));
    return ok;
}
static int wx_space_run(wx_address_space *p,u32 expected_pid,u32 expected_exit){
    if(!p||!p->root||wx_active)return 0;
    wx_load_cr3((u64)p->root);
    int isolated=*(volatile u32*)(WX_USER_CODE+0x100)==expected_pid;
    /* No forbidden user bits are set in the low identity mapped kernel. */
    const u64 *pdpt=(const u64*)(p->root[0]&PG_MASK);
    for(unsigned i=0;i<4;i++)if(pdpt[i]&PG_USER)isolated=0;
    wx_active=1;wx_exit_requested=0;wx_exit_code=0;
    wx_fault_vector=0;wx_fault_error=0;wx_write_count=0;
    u32 actual=wx_enter_user(WX_USER_CODE,WX_USER_STACK+4096-16);
    wx_active=0;
    return isolated && actual==expected_exit && wx_exit_requested && !wx_fault_vector;
}
static int wx_address_space_test(void){
    if(wx_active||!pg_ready)return 0;
    static int tss_ready;
    if(!tss_ready){if(!wx_prepare_tss())return 0;tss_ready=1;}
    const u64 old_cr3=wx_read_cr3();
    const u32 saved_used=pg_used;
    pg_zero(wx_spaces,sizeof(wx_spaces));
    int ok=wx_space_make(&wx_spaces[0],101,21)&&
           wx_space_make(&wx_spaces[1],202,42);
    if(ok){
        ok=wx_spaces[0].root!=wx_spaces[1].root &&
           wx_spaces[0].code!=wx_spaces[1].code &&
           wx_spaces[0].stack!=wx_spaces[1].stack;
        if(ok)ok=wx_space_run(&wx_spaces[0],101,21);
        wx_load_cr3(old_cr3);
        if(ok)ok=wx_space_run(&wx_spaces[1],202,42);
        wx_load_cr3(old_cr3);
        if(ok)ok=wx_space_run(&wx_spaces[0],101,21);
        wx_load_cr3(old_cr3);
    }
    wx_load_cr3(old_cr3);
    for(unsigned i=0;i<WX_SPACE_COUNT;i++)
        if(wx_spaces[i].root && !wx_space_destroy(&wx_spaces[i]))ok=0;
    if(pg_used!=saved_used)ok=0;
    if(ok)print("PROC_SPACE_OK: 2 independent CR3 roots, same VA different frames, CPL3 exits 21/42/21, resources restored\n");
    else print("PROC_SPACE_FAILED\n");
    return ok;
}
#endif
