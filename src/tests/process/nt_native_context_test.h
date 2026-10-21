/* Exercise state across separate native call scopes and loader remapping. */
static int nt_native_context_test(void){
 const ram_file *cmd=find_file("cmd.exe"),*hello=find_file("hello.exe");if(!cmd||!hello||nt_native.module)return 0;
 unsigned pages_before=0,regions_before=0;
 for(unsigned i=0;i<PAGE_POOL_COUNT;i++)pages_before+=page_used[i]!=0;
 for(unsigned i=0;i<16;i++)regions_before+=vm_regions[i].size!=0;
 u64 gs_before=read_msr(0xc0000101);int allowed_before=nt_syscall_allowed;
 for(unsigned cycle=0;cycle<2;cycle++){
  pe_load(cmd,0);pe_module *m=module_named("ntdll.dll");
  if(!nt_native_start(m)||!nt_native.active||nt_native_start(m)||read_msr(0xc0000101)!=gs_before||nt_syscall_allowed!=allowed_before)return 0;
  u64 allocate=module_export(m,"RtlAllocateHeap",0,0);if(!allocate)return 0;
  nt_native_scope scope;nt_native_enter(&scope);
  void *block=((void*(*)(void*,u32,u64))allocate)(nt_native.heap,8,64);
  int ok=block!=0;
  if(block)for(unsigned i=0;i<64;i++){if(((u8*)block)[i])ok=0;((u8*)block)[i]=(u8)(i^0x5a);}
  nt_native_leave(&scope);
  if(!ok||read_msr(0xc0000101)!=gs_before||nt_syscall_allowed!=allowed_before)return 0;
  /* The PEB heap and allocations survive leaving a native call scope. */
  if(rd64(app_peb+0x30)!=(u64)nt_native.heap)return 0;
  nt_native_enter(&scope);
  for(unsigned i=0;i<64;i++)if(((u8*)block)[i]!=(u8)(i^0x5a))ok=0;
  if(!nt_process_parameters_test(m,nt_native.heap)||rd64(app_peb+0x30)!=(u64)nt_native.heap)ok=0;
  nt_native_leave(&scope);if(!ok)return 0;
  /* Real second NTDLL heap: rejected disposal must preserve both heaps and
   * keep the module arena alive so its owner can release it and retry. */
  u64 create=module_export(m,"RtlCreateHeap",0,0),destroy=module_export(m,"RtlDestroyHeap",0,0);
  if(!create||!destroy)return 0;
  void *process_heap=nt_native.heap;unsigned locks=nt_native.locks;
  u8 *command_buffer=kb_inputs.command.data;
  u64 acp=kb_nls.acp,oem=kb_nls.oem;pe_module *nls_module=kb_nls.module;
  u64 console_handles[3];for(unsigned i=0;i<3;i++)console_handles[i]=nt_console.handles[i];
  nt_native_enter(&scope);
  void *private_heap=((void*(*)(u32,void*,u64,u64,void*,void*))create)(2,0,0x100000,0x1000,0,0);
  nt_native_leave(&scope);
  if(!private_heap||rd32(app_peb+0xe8)!=2)return 0;
  if(nt_native_dispose()||nt_native.heap!=process_heap||!nt_native.active||nt_native.module!=m||nt_native.locks!=locks||
     rd64(app_peb+0x30)!=(u64)process_heap||rd32(app_peb+0xe8)!=2||
     read_msr(0xc0000101)!=gs_before||nt_syscall_allowed!=allowed_before)return 0;
  for(unsigned i=0;i<64;i++)if(((u8*)block)[i]!=(u8)(i^0x5a))return 0;
  nt_native_enter(&scope);
  void *failed=((void*(*)(void*))destroy)(private_heap);
  nt_native_leave(&scope);
  if(failed||rd32(app_peb+0xe8)!=1||rd64(m->base+0x16ad40)!=(u64)process_heap)return 0;
  if(!kb_inputs.module||kb_inputs.command.data!=command_buffer||!command_buffer)return 0;
  if(!nt_console.active||!acp||!oem||kb_nls.module!=nls_module||kb_nls.acp!=acp||kb_nls.oem!=oem)return 0;
  for(unsigned i=0;i<3;i++)if(!obj_lookup(&nt_objects,console_handles[i])||rd64(app_parameters+0x20+i*8)!=console_handles[i])return 0;
  print("NT_PRIVATE_HEAP_GUARD_OK\n");
  /* Leave a live allocation: context disposal owns the heap, not just the block.
   * The loader must dispose before overwriting NTDLL or zeroing the PEB. */
  if(cycle==1){
   if(!runtime_reset("cmd.exe")||nt_native.module||nt_native.heap||rd64(app_peb+0x30)||module_named("ntdll.dll")!=m)return 0;
  }
  pe_load(hello,0);
  if(nt_console.active||kb_nls.module||kb_inputs.module)return 0;
  for(unsigned i=0;i<3;i++)if(obj_lookup(&nt_objects,console_handles[i]))return 0;
  if(nt_native.module||nt_native.heap||nt_native.active||nt_native.locks||rd64(app_peb+0x30)||rd64(app_peb+0x38)||module_count!=1||!case_same(modules[0].file->name,"hello.exe"))return 0;
  if(!nt_native_dispose()||read_msr(0xc0000101)!=gs_before||nt_syscall_allowed!=allowed_before)return 0;
  unsigned pages_after=0,regions_after=0;
  for(unsigned i=0;i<PAGE_POOL_COUNT;i++)pages_after+=page_used[i]!=0;
  for(unsigned i=0;i<16;i++)regions_after+=vm_regions[i].size!=0;
  if(pages_after!=pages_before||regions_after!=regions_before)return 0;
  nt_partial_values("NT_NATIVE_CONTEXT","cycle",(u64[]){cycle,1,1,1},4);
 }
 print("NT_NATIVE_CONTEXT_OK: persistent PEB heap across calls, parameter consumer, loader teardown, live allocation reclaimed, restart\n");
 return 1;
}
