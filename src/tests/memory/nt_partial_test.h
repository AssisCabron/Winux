/* Compare partial release through original NTDLL stubs with the host probe. */
static void nt_partial_values(const char *kind,const char *label,const u64 *values,unsigned count){
 print(kind);print(" ");print(label);print(" ");for(unsigned i=0;i<count;i++){if(i)print(",");number(values[i]);}print("\n");
}
static int nt_partial_test(void){
 const ram_file *cmd=find_file("cmd.exe");if(!cmd||!find_file("ntdll.dll"))return 0;
 pe_load(cmd,0);pe_module *m=module_named("ntdll.dll");if(!m||m->state!=2)return 0;
 typedef u32(*allocate_t)(void*,void**,u64,u64*,u32,u32);
 typedef u32(*free_t)(void*,void**,u64*,u32);
 typedef u32(*query_t)(void*,void*,u32,void*,u64,u64*);
 allocate_t allocate=(allocate_t)nt_checked_stub(m,"NtAllocateVirtualMemory",24);
 free_t release=(free_t)nt_checked_stub(m,"NtFreeVirtualMemory",30);
 query_t query=(query_t)nt_checked_stub(m,"NtQueryVirtualMemory",35);
 if(!allocate||!release||!query||!nt_syscall_init())return 0;
 const char *labels[]={"prefix","middle","suffix","unaligned"};
 const u64 offsets[]={0,8192,28672,4097},sizes[]={4096,8192,4096,1};
 int ok=1;nt_syscall_allowed=1;
 for(unsigned test=0;test<4;test++){
  void *base=0;u64 bytes=32768;
  if(allocate((void*)~0ULL,&base,0,&bytes,0x3000,4)||!base){ok=0;break;}
  for(unsigned i=0;i<8;i++)((volatile u8*)base)[i*4096]=(u8)(40+i);
  void *address=(void*)((u64)base+offsets[test]);bytes=sizes[test];
  u32 s=release((void*)~0ULL,&address,&bytes,0x8000);
  u64 fields[]={s,(u64)address-(u64)base,bytes};nt_partial_values("NT_PARTIAL",labels[test],fields,3);
  if(s)ok=0;
  for(unsigned i=0;i<8;i++){
   vm_basic_info q;u64 returned=0;
   if(query((void*)~0ULL,(void*)((u64)base+i*4096),0,&q,sizeof(q),&returned)||returned!=48){ok=0;break;}
   u64 values[7]={i,q.state};unsigned count=2;
   if(q.state!=0x10000){
    if(q.state!=0x1000){ok=0;break;}
    values[2]=q.allocation-(u64)base;values[3]=q.base-(u64)base;values[4]=q.region_size;values[5]=q.protect;
    values[6]=((volatile u8*)base)[i*4096];if(values[6]!=40+i)ok=0;count=7;
   }nt_partial_values("NT_PARTIAL_PAGE",labels[test],values,count);
  }
  /* Every fragment is a separate allocation after release; clean all of them. */
  for(unsigned i=0;i<16;i++)if(vm_regions[i].size){address=(void*)vm_regions[i].base;bytes=0;if(release((void*)~0ULL,&address,&bytes,0x8000)){ok=0;vm_release(vm_regions[i].base);}}
 }
 nt_syscall_allowed=0;
 /* Splitting with no free metadata must fail before changing any page. */
 u64 a=vm_reserve(32768);if(!a||!vm_commit(a,32768,3))ok=0;
 if(a){
  for(unsigned i=1;i<16;i++)if(!vm_reserve(4096))ok=0;
  if(vm_release_span(a+8192,4096)||!vm_find(a,32768))ok=0;
 }
 for(unsigned i=0;i<16;i++)if(vm_regions[i].size)vm_release(vm_regions[i].base);
 /* RtlCreateHeap can trim 31*64 KiB from a 2 MiB reservation. */
 a=vm_reserve(0x200000);
 if(!a||!vm_release_span(a,0x1f0000)||!vm_find(a+0x1f0000,0x10000)||!vm_commit(a+0x1f0000,8192,3))ok=0;
 for(unsigned i=0;i<16;i++)if(vm_regions[i].size)vm_release(vm_regions[i].base);
 if(!pool_test())ok=0;return ok;
}
