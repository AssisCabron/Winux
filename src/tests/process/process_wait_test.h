/* Native timer-wait contract on the existing Process Manager.
 * Service bytecode uses WX_NATIVE_SLEEP_TICKS through CPL3 INT80;
 * this test additionally verifies invalid durations, exclusion from
 * ready queue, PIT-driven wake and exact resource reaping. */
#ifndef WINUX_PROCESS_WAIT_TEST_H
#define WINUX_PROCESS_WAIT_TEST_H
static int wx_process_wait_test(void){
    if(wx_active || !pg_ready || wx_proc_current_irq)return 0;
    const u32 free_before=wx_proc_free_pages();
    const u32 waits_before=wx_proc_sleep_calls;
    const u32 wakes_before=wx_proc_wake_count;
    const u32 invalid_before=wx_proc_invalid_sleeps;
    wx_managed_process *p=wx_proc_spawn(77);
    if(!p)return 0;
    int ok=1;
    p->state=WX_PROC_RUNNING;
    wx_proc_current_irq=p;
    wx_active=1;
    if(wx_proc_request_sleep(0) || wx_proc_request_sleep(1001) ||
       !wx_proc_request_sleep(4))ok=0;
    wx_active=0;wx_proc_current_irq=0;
    u64 until=p->next_ready_tick;
    if(p->state!=WX_PROC_BLOCKED)ok=0;
    wx_proc_wake_blocked();
    if(timer_ticks<until && p->state!=WX_PROC_BLOCKED)ok=0;
    /* Wait for the REAL PIT, no synthetic tick manipulation. */
    for(unsigned n=0;n<64 && timer_ticks<until;n++)
        __asm__ volatile("sti; hlt":::"memory");
    if(timer_ticks<until)ok=0;
    wx_proc_wake_blocked();
    if(p->state!=WX_PROC_READY)ok=0;
    if(wx_proc_sleep_calls!=waits_before+1 ||
       wx_proc_wake_count<wakes_before+1 ||
       wx_proc_invalid_sleeps!=invalid_before+2)ok=0;
    p->state=WX_PROC_EXITED;
    if(!wx_proc_reap(p) || wx_proc_free_pages()!=free_before)ok=0;
    if(ok)print("THREAD_WAIT_OK: invalid durations rejected, BLOCKED excluded until PIT deadline, READY wake, owner reaped\n");
    else print("THREAD_WAIT_FAILED\n");
    return ok;
}
#endif
