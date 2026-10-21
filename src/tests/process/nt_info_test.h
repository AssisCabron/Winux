static int nt_info_unchanged(const u8 *p,unsigned first,unsigned end){for(unsigned i=first;i<end;i++)if(p[i]!=0xa5)return 0;return 1;}
static int nt_heap_seed_test(void){
 const ram_file *cmd=find_file("cmd.exe");if(!cmd||!find_file("ntdll.dll"))return 0;
 pe_load(cmd,0);pe_module *m=module_named("ntdll.dll");if(!m||m->state!=2||m->size<0x16d5e8)return 0;
 /* Version-bound 33-byte wrapper from the retained REA dossier. */
 const u8 expected[]={0x40,0x53,0x48,0x83,0xec,0x20,0xe8,0xd5,0x03,0,0,0x8b,0xd8,0x48,0xc1,0xe3,0x20,0xe8,0xca,0x03,0,0,0x8b,0xc0,0x48,0x0b,0xc3,0x48,0x83,0xc4,0x20,0x5b,0xc3};
 const u8 *code=m->base+0xa068;
 for(unsigned i=0;i<sizeof(expected);i++)if(code[i]!=expected[i])return 0;
 if(!nt_checked_stub(m,"NtQueryInformationProcess",25)||!nt_syscall_init())return 0;
 if(rd32(m->base+0x16d230)||rd64(m->base+0x16d5e0))return 0;
 u32 before=nt_syscall_counts[5];nt_syscall_allowed=1;
 u64 first=((u64(*)(void))code)(),second=((u64(*)(void))code)();
 nt_syscall_allowed=0;
 if(nt_syscall_counts[5]-before!=2||!rd32(m->base+0x16d230)||!rd64(m->base+0x16d5e0))return 0;
 /* Values are intentionally not logged or claimed to be cryptographically random. */
 (void)first;(void)second;
 print("NT_HEAP_SEED_EVIDENCE: original helpers returned; RunOnce state initialized; 2 process-query SYSCALLs\n");return 1;
}
/* Query outputs must work in committed guest VM, including page crossings.
 * These are guest adapter invariants, not additional Windows oracle records. */
static int nt_info_vm_test(pe_module *m){
 typedef u32(*process_t)(void*,u32,void*,u32,u32*);
 typedef u32(*system_t)(u32,void*,u32,u32*);
 typedef u32(*query_t)(void*,void*,u32,void*,u64,u64*);
 process_t process=(process_t)nt_checked_stub(m,"NtQueryInformationProcess",25);
 system_t system=(system_t)nt_checked_stub(m,"NtQuerySystemInformation",54);
 query_t query=(query_t)nt_checked_stub(m,"NtQueryVirtualMemory",35);
 if(!process||!system||!query)return 0;
 u64 base=vm_reserve(12288);if(!base)return 0;
 int ok=vm_commit(base,8192,3);if(!ok){vm_release(base);return 0;}
 u8 *p=(u8*)base;u32 *returned=(u32*)(p+128);u64 *wide_returned=(u64*)(p+136);
 nt_syscall_allowed=1;
 if(process((void*)~0ULL,0x24,p+4094,4,returned)||*returned!=4)ok=0;
 if(system(0,p+4064,64,returned)||*returned!=64||((nt_system_basic*)(p+4064))->page_size!=4096)ok=0;
 if(query((void*)~0ULL,p,0,p+4072,sizeof(vm_basic_info),wide_returned)||*wide_returned!=sizeof(vm_basic_info))ok=0;
 for(unsigned i=4064;i<4096;i++)p[i]=0xa5;
 if(!vm_protect(base+4096,4096,1))ok=0;
 if(system(0,p+4064,64,returned)!=0xc0000005U||!nt_info_unchanged(p,4064,4096))ok=0;
 if(process((void*)~0ULL,0x24,p+4094,4,returned)!=0xc0000005U)ok=0;
 if(query((void*)~0ULL,p,0,p+4072,sizeof(vm_basic_info),wide_returned)!=0xc0000005U)ok=0;
 if(process((void*)~0ULL,0x24,p+256,4,(u32*)(p+4096))!=0xc0000005U)ok=0;
 if(!vm_decommit(base+4096,4096))ok=0;
 if(system(0,p+4064,64,returned)!=0xc0000005U)ok=0;
 if(process((void*)~0ULL,0x24,p+8192,4,returned)!=0xc0000005U)ok=0;
 if(!vm_commit(base+4096,4096,3)||system(0,p+4064,64,returned))ok=0;
 if(!vm_release(base))ok=0;
 if(process((void*)~0ULL,0x24,p,4,0)!=0xc0000005U)ok=0;
 nt_syscall_allowed=0;
 if(ok)print("NT_INFO_VM_OK: cross-page outputs, RO rejection, decommit, recommit, release\n");
 return ok;
}
static int nt_info_test(void){
 const ram_file *cmd=find_file("cmd.exe");if(!cmd||!find_file("ntdll.dll"))return 0;
 pe_load(cmd,0);pe_module *m=module_named("ntdll.dll");if(!m||m->state!=2)return 0;
 typedef u32(*process_t)(void*,u32,void*,u32,u32*);
 typedef u32(*system_t)(u32,void*,u32,u32*);
 process_t process=(process_t)nt_checked_stub(m,"NtQueryInformationProcess",25);
 system_t system=(system_t)nt_checked_stub(m,"NtQuerySystemInformation",54);
 if(!process||!system||!nt_syscall_init())return 0;
 const u32 lengths[]={0,3,4,8,0,63,64,72};
 const char *labels[]={"process-0","process-3","process-4","process-8","system-0","system-63","system-64","system-72"};
 u32 cookie=0,process_before=nt_syscall_counts[5],system_before=nt_syscall_counts[6];int ok=1;
 nt_syscall_allowed=1;
 for(unsigned i=0;i<8;i++){
  u8 buffer[80] __attribute__((aligned(16)));for(unsigned j=0;j<80;j++)buffer[j]=0xa5;
  u32 returned=0x1234,s=i<4?process((void*)~0ULL,0x24,buffer,lengths[i],&returned):system(0,buffer,lengths[i],&returned);
  u64 values[8]={s,returned};unsigned count=2;
  if(!s){
   if(i<4){if(!cookie)cookie=*(u32*)buffer;values[2]=*(u32*)buffer==cookie;values[3]=nt_info_unchanged(buffer,4,80);count=4;}
   else{
    nt_system_basic *q=(nt_system_basic*)buffer;
    values[2]=q->page_size;values[3]=q->granularity;values[4]=q->minimum<q->maximum;values[5]=q->physical_pages>0;values[6]=q->processors>0;values[7]=nt_info_unchanged(buffer,64,80);count=8;
    if(q->processors!=1||q->affinity!=1||q->maximum!=VM_WINDOW_END-1||q->physical_pages>q->highest_page-q->lowest_page+1)ok=0;
    print("NT_INFO_GUEST pages=");number(q->physical_pages);print(" processors=");number(q->processors);print("\n");
   }
  }else{values[2]=nt_info_unchanged(buffer,0,80);count=3;}
  nt_partial_values("NT_INFO",labels[i],values,count);
 }
 u32 first=0,second=0;
 if(process((void*)~0ULL,0x24,&first,4,0)||process((void*)~0ULL,0x24,&second,4,0)||first!=second||first!=cookie)ok=0;
 nt_partial_values("NT_INFO","cookie-stable",(u64[]){first==second&&first==cookie},1);
 nt_syscall_allowed=0;
 if(nt_syscall_counts[5]-process_before!=6||nt_syscall_counts[6]-system_before!=4)ok=0;
 return nt_info_vm_test(m)&&ok;
}
