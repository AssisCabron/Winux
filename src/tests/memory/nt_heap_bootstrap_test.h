/* Version-bound integration diagnostic; never calls DLL entry points or CMD. */
static int nt_heap_bootstrap_test(void){
 unsigned pages_before=0,regions_before=0;
 for(unsigned i=0;i<PAGE_POOL_COUNT;i++)pages_before+=page_used[i]!=0;
 for(unsigned i=0;i<16;i++)regions_before+=vm_regions[i].size!=0;
 if(!nt_nls_test()||!nt_syscall_init())return 0;
 pe_module *m=module_named("ntdll.dll");if(!m||m->state!=2||m->size<0x16db48)return 0;
 const char *names[]={"RtlInitializeSListHead","RtlInterlockedPushListSList","RtlInitializeCriticalSectionEx","RtlCreateHeap","RtlAllocateHeap","RtlSizeHeap","RtlFreeHeap","RtlDestroyHeap","RtlReAllocateHeap"};
 u64 fn[9];for(unsigned i=0;i<9;i++){fn[i]=module_export(m,names[i],0,0);if(!fn[i])return 0;}
 u8 *pool=m->base+0x16ae40,*head=m->base+0x16d350;
 ((void(*)(void*))fn[0])(head);
 for(unsigned i=0;i<8;i++)pp_put64(pool+i*0x30,i==7?0:(u64)(pool+(i+1)*0x30));
 ((void*(*)(void*,void*,void*,u32))fn[1])(head,pool,pool+7*0x30,8);
 /* Explicit experimental policy, based on the recorded host profile. */
 pp_put64(app_peb+0xc8,0x100000);pp_put64(app_peb+0xd0,0x2000);
 pp_put64(app_peb+0xd8,0x10000);pp_put64(app_peb+0xe0,0x1000);
 pp_put32(app_peb+0xe8,0);pp_put32(app_peb+0xec,16);pp_put64(app_peb+0xf0,(u64)m->base+0x16ad40);
 pp_put64(app_peb+0xa0,(u64)find_file("c_1252.nls")->data);
 pp_put64(app_peb+0xa8,(u64)find_file("c_850.nls")->data);
 pp_put64(app_peb+0xb0,(u64)find_file("l_intl.nls")->data);
 u64 old_gs=read_msr(0xc0000101);u16 old_selector,selector=16;
 __asm__ volatile("mov %%gs,%0":"=r"(old_selector));__asm__ volatile("mov %0,%%gs"::"r"(selector):"memory");
 write_msr(0xc0000101,(u64)app_teb);
 __asm__ volatile("xor %%eax,%%eax; cpuid":::"rax","rbx","rcx","rdx","memory");
 int ok=0;void *heap=0,*block=0;
 typedef u32(*init_t)(void*,u32,u32);
 if(((init_t)fn[2])(m->base+0x16c0e0,0,0x10000000)||((init_t)fn[2])(m->base+0x16ace0,0,0x10000000))goto done;
 pp_put64(app_peb+0x38,(u64)m->base+0x16c0e0);
 nt_syscall_allowed=1;
 print("NT_HEAP_BOOTSTRAP: create private heap\n");
 heap=((void*(*)(u32,void*,u64,u64,void*,void*))fn[3])(2,0,0x100000,0x1000,0,0);
 nt_partial_values("NT_HEAP_BOOTSTRAP","create",(u64[]){heap!=0,rd32(app_teb+0x1250),rd32(app_peb+0xe8)},3);
 if(!heap||rd32(app_peb+0xe8)!=1||rd64(m->base+0x16ad40)!=(u64)heap||rd64(app_peb+0x30))goto done;
 nt_partial_values("NT_HEAP_BATCH","create",(u64[]){1},1);
 block=((void*(*)(void*,u32,u64))fn[4])(heap,8,257);
 if(!block)goto done;
 for(unsigned i=0;i<257;i++)if(((u8*)block)[i])goto done;
 if(((u64(*)(void*,u32,void*))fn[5])(heap,0,block)!=257)goto done;
 for(unsigned i=0;i<257;i++)((u8*)block)[i]=(u8)i;
 if(!((u8(*)(void*,u32,void*))fn[6])(heap,0,block))goto done;
 block=0;
 {const u64 sizes[]={0,1,15,16,4096,65536,3*1024*1024};
 for(unsigned k=0;k<sizeof(sizes)/sizeof(sizes[0]);k++){
  u64 size=sizes[k];print("NT_HEAP_BATCH_TRY ");number(size);print("\n");
  block=((void*(*)(void*,u32,u64))fn[4])(heap,8,size);
  if(!block)goto done;
  int zeroed=1;for(u64 i=0;i<size;i++)if(((u8*)block)[i]){zeroed=0;break;}
  u64 reported=((u64(*)(void*,u32,void*))fn[5])(heap,0,block);
  nt_partial_values("NT_HEAP_BATCH","allocate",(u64[]){size,((u64)block&15)==0,zeroed,reported},4);
  if(((u64)block&15)||!zeroed||reported!=size)goto done;
  for(u64 i=0;i<size;i++)((u8*)block)[i]=(u8)(i^0x5a);
  u8 freed=((u8(*)(void*,u32,void*))fn[6])(heap,0,block);
  nt_partial_values("NT_HEAP_BATCH","free",(u64[]){size,freed!=0},2);
  if(!freed)goto done;block=0;
 }}
 block=((void*(*)(void*,u32,u64))fn[4])(heap,0,32);if(!block)goto done;
 for(unsigned i=0;i<32;i++)((u8*)block)[i]=0x5a;
 {void *grown=((void*(*)(void*,u32,void*,u64))fn[8])(heap,8,block,8192);if(!grown)goto done;block=grown;
 int preserved=1,zero_tail=1;
 for(unsigned i=0;i<32;i++)if(((u8*)block)[i]!=0x5a)preserved=0;
 for(unsigned i=32;i<8192;i++)if(((u8*)block)[i])zero_tail=0;
 u64 reported=((u64(*)(void*,u32,void*))fn[5])(heap,0,block);
 nt_partial_values("NT_HEAP_BATCH","reallocate",(u64[]){preserved,zero_tail,reported},3);
 if(!preserved||!zero_tail||reported!=8192)goto done;}
 {u8 freed=((u8(*)(void*,u32,void*))fn[6])(heap,0,block);
 nt_partial_values("NT_HEAP_BATCH","free_reallocated",(u64[]){freed!=0},1);if(!freed)goto done;block=0;
 freed=((u8(*)(void*,u32,void*))fn[6])(heap,0,0);
 nt_partial_values("NT_HEAP_BATCH","free_null",(u64[]){freed!=0},1);if(!freed)goto done;}
 if(!nt_process_parameters_test(m,heap))goto done;
 if(((void*(*)(void*))fn[7])(heap))goto done;
 nt_partial_values("NT_HEAP_BATCH","destroy",(u64[]){1},1);
 heap=0;ok=rd32(app_peb+0xe8)==0;
 {unsigned pages_after=0,regions_after=0;
 for(unsigned i=0;i<PAGE_POOL_COUNT;i++)pages_after+=page_used[i]!=0;
 for(unsigned i=0;i<16;i++)regions_after+=vm_regions[i].size!=0;
 nt_partial_values("NT_HEAP_BOOTSTRAP","resources",(u64[]){pages_after==pages_before,regions_after==regions_before,rd32(app_peb+0xe8)},3);
 if(pages_after!=pages_before||regions_after!=regions_before)ok=0;}
 {u64 remove=module_export(m,"RtlDeleteCriticalSection",0,0),depth=module_export(m,"RtlQueryDepthSList",0,0);
 if(!remove||!depth)ok=0;
 else if(((u32(*)(void*))remove)(m->base+0x16ace0)||((u32(*)(void*))remove)(m->base+0x16c0e0)||((u16(*)(void*))depth)(head)!=8)ok=0;
 pp_put64(app_peb+0x38,0);}
 if(ok)print("NT_HEAP_LIFECYCLE_OK: original create/zero-allocate/size/free/destroy, registry empty, pages and pool restored\n");
 done:
 /* On failure, retain state for diagnosis; reboot before retrying this diagnostic. */
 nt_syscall_allowed=0;
 __asm__ volatile("mov %0,%%gs"::"r"(old_selector):"memory");write_msr(0xc0000101,old_gs);
 return ok&&read_msr(0xc0000101)==old_gs;
}
