static unsigned nt_fls_callbacks;
static u64 nt_fls_last;
static void nt_fls_callback(void *value){nt_fls_callbacks++;nt_fls_last=(u64)value;}
static int nt_fls_test(void){
 const ram_file *cmd=find_file("cmd.exe");if(!cmd||nt_native.module)return 0;
 unsigned pages_before=0,regions_before=0;
 for(unsigned i=0;i<PAGE_POOL_COUNT;i++)pages_before+=page_used[i]!=0;
 for(unsigned i=0;i<16;i++)regions_before+=vm_regions[i].size!=0;
 pe_load(cmd,0);pe_module *m=module_named("ntdll.dll");if(!nt_native_start(m))return 0;
 typedef u32(*alloc_t)(void(*)(void*),u32*);
 typedef u32(*set_t)(u32,void*);typedef u32(*get_t)(u32,void**);typedef u32(*free_t)(u32);
 alloc_t allocate=(alloc_t)module_export(m,"RtlFlsAlloc",0,0);
 set_t set=(set_t)module_export(m,"RtlFlsSetValue",0,0);
 get_t get=(get_t)module_export(m,"RtlFlsGetValue",0,0);
 free_t release=(free_t)module_export(m,"RtlFlsFree",0,0);
 if(!allocate||!set||!get||!release)return 0;
 nt_native_scope scope;nt_native_enter(&scope);int ok=0;u32 indices[32],status,index;
 void *out=0;nt_fls_callbacks=0;nt_fls_last=0;
 for(unsigned i=0;i<32;i++){
  indices[i]=~0U;if(allocate(i<2?nt_fls_callback:0,&indices[i])||indices[i]==~0U)goto done;
  for(unsigned j=0;j<i;j++)if(indices[i]==indices[j])goto done;
 }
 nt_partial_values("NT_FLS","allocate",(u64[]){32,1},2);
 /* Match the host fixture's explicit initialization, not its preexisting TEB. */
 if(set(indices[0],0))goto done;
 out=(void*)0xaaaa;status=get(indices[0],&out);
 nt_partial_values("NT_FLS","get-empty",(u64[]){status,(u64)out},2);if(status||out)goto done;
 status=set(indices[0],(void*)0x1111);
 nt_partial_values("NT_FLS","set-first",(u64[]){status,nt_fls_callbacks},2);if(status||nt_fls_callbacks)goto done;
 status=set(indices[0],(void*)0x2222);
 nt_partial_values("NT_FLS","set-overwrite",(u64[]){status,nt_fls_callbacks},2);if(status||nt_fls_callbacks)goto done;
 status=get(indices[0],&out);nt_partial_values("NT_FLS","get-value",(u64[]){status,(u64)out},2);
 if(status||out!=(void*)0x2222)goto done;
 status=release(indices[0]);nt_partial_values("NT_FLS","free-callback",(u64[]){status,nt_fls_callbacks,nt_fls_last},3);
 if(status||nt_fls_callbacks!=1||nt_fls_last!=0x2222)goto done;
 status=release(indices[1]);nt_partial_values("NT_FLS","free-empty",(u64[]){status,nt_fls_callbacks},2);
 if(status||nt_fls_callbacks!=1)goto done;
 out=(void*)0xaaaa;status=get(~0U,&out);nt_partial_values("NT_FLS","invalid-get",(u64[]){status,(u64)out},2);
 if(status!=0xc000000dU||out!=(void*)0xaaaa)goto done;
 status=set(~0U,0);nt_partial_values("NT_FLS","invalid-set",(u64[]){status},1);if(status!=0xc000000dU)goto done;
 status=release(~0U);nt_partial_values("NT_FLS","invalid-free",(u64[]){status},1);if(status!=0xc000000dU)goto done;
 for(unsigned i=32;i>2;i--)if(release(indices[i-1]))goto done;
 nt_partial_values("NT_FLS","cleanup",(u64[]){nt_fls_callbacks},1);
 /* Additional guest-only lifecycle: leave a callback value for thread cleanup. */
 if(allocate(nt_fls_callback,&index)||set(index,(void*)0x3333)||!rd64(app_teb+0x17c8))goto done;
 ok=1;
 done:nt_native_leave(&scope);
 if(!ok)return 0; /* Retain failure state for diagnosis; reboot before retry. */
 if(!nt_native_dispose()||rd64(app_teb+0x17c8)||nt_fls_callbacks!=2||nt_fls_last!=0x3333)return 0;
 unsigned pages_after=0,regions_after=0;
 for(unsigned i=0;i<PAGE_POOL_COUNT;i++)pages_after+=page_used[i]!=0;
 for(unsigned i=0;i<16;i++)regions_after+=vm_regions[i].size!=0;
 if(pages_before!=pages_after||regions_before!=regions_after)return 0;
 print("NT_FLS_TEARDOWN_OK: original ProcessFlsData callback, TEB cleared, list unlinked, heap resources restored\n");
 return 1;
}
