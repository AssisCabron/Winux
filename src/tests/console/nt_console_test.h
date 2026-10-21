/* Force partial open failure and verify transactional publication/cleanup. */
static int nt_console_rollback_test(void){
 u64 events[OBJ_SLOTS-2],previous[3];unsigned count=0;int ok=1;
 for(unsigned i=0;i<3;i++)previous[i]=rd64(app_parameters+0x20+i*8);
 for(;count<OBJ_SLOTS-2;count++)if(obj_event_create(&nt_objects,OBJ_EVENT_ALL,0,0,&events[count]))break;
 if(count!=OBJ_SLOTS-2||nt_console_start()||nt_console.active)ok=0;
 unsigned live=0;for(unsigned i=0;i<OBJ_SLOTS;i++)live+=nt_objects.slots[i].kind!=0;
 if(live!=count)ok=0;
 for(unsigned i=0;i<3;i++)if(rd64(app_parameters+0x20+i*8)!=previous[i])ok=0;
 for(unsigned i=0;i<count;i++)if(obj_close(&nt_objects,events[i]))ok=0;
 return ok;
}
static int nt_console_test(void){
 const ram_file *cmd=find_file("cmd.exe");if(!cmd||nt_native.module)return 0;
 pe_load(cmd,0);pe_module *m=module_named("ntdll.dll"),*k=module_named("kernelbase.dll");
 if(!k||!nt_console_rollback_test()||!nt_native_start(m))return 0;
 typedef int(*transfer_t)(void*,void*,u32,u32*,void*);
 transfer_t read=(transfer_t)module_export(k,"ReadFile",0,0),write=(transfer_t)module_export(k,"WriteFile",0,0);
 u64 std=module_export(k,"GetStdHandle",0,0),type=module_export(k,"GetFileType",0,0);
 u64 close=nt_checked_stub(m,"NtClose",15);if(!read||!write||!std||!type||!close)return 0;
 if(!nt_console.active)return 0;
 nt_console.device.diagnostic=1;
 u64 handles[3],previous[3];for(unsigned i=0;i<3;i++){handles[i]=nt_console.handles[i];previous[i]=nt_console.previous[i];}
 int ok=0;nt_native_scope scope;nt_native_enter(&scope);
 for(unsigned i=0;i<3;i++)if(((void*(*)(u32))std)((u32)-10-i)!=(void*)handles[i]||((u32(*)(void*))type)((void*)handles[i])!=2)goto done;
 char message[]="NT_CONSOLE_OUTPUT: original WriteFile\n";u32 written=0;
 if(!write((void*)handles[1],message,sizeof(message)-1,&written,0)||written!=sizeof(message)-1)goto done;
 u8 input[32];for(unsigned i=0;i<32;i++)input[i]=0xa5;u32 first=0,second=0;u64 before,after,ticks=timer_ticks;
 __asm__ volatile("pushfq; pop %0":"=r"(before));
 if(!read((void*)handles[0],input,4,&first,0)||first!=4)goto done;
 /* Leave the calling scope between partial reads; endpoint and line survive. */
 nt_native_leave(&scope);nt_native_enter(&scope);
 if(!read((void*)handles[0],input+4,28,&second,0)||second!=11)goto done;
 __asm__ volatile("pushfq; pop %0":"=r"(after));
 const char *expected="console-input\r\n";for(unsigned i=0;i<15;i++)if(input[i]!=(u8)expected[i])goto done;
 if(!nt_info_unchanged(input,15,32)||!nt_console.device.waits||timer_ticks<=ticks||((before^after)&0x200))goto done;
 print("NT_CONSOLE_READ ");nt_file_hexbytes(input,15);print("\n");
 ok=1;
 done:
 /* Closing stdout early and reusing its slot must not let context cleanup
  * close the new event. The other two standard handles remain context-owned. */
 u64 event=0;
 if(((u32(*)(void*))close)((void*)handles[1])||obj_event_create(&nt_objects,OBJ_EVENT_ALL,0,0,&event))ok=0;
 nt_native_leave(&scope);if(!nt_native_dispose())return 0;
 for(unsigned i=0;i<3;i++)if(obj_lookup(&nt_objects,handles[i])||rd64(app_parameters+0x20+i*8)!=previous[i])ok=0;
 if(nt_console.active||nt_console.device.line.length||!obj_lookup(&nt_objects,event))ok=0;
 if(event&&obj_close(&nt_objects,event))ok=0;
 if(ok)print("NT_CONSOLE_PERSISTENT_OK: scopes, early close, slot reuse, context cleanup\n");
 if(ok)print("NT_CONSOLE_LIFECYCLE_OK: delayed input, timer awake, IF restored, PEB handles restored and closed\n");return ok;
}
