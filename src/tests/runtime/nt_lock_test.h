/* Version-bound, uncontended bootstrap diagnostic. Never enables CMD startup. */
static void nt_lock_record(const char *step,u32 flags,u32 status,const u8 *cs){
 u64 v[8]={flags,status};unsigned n=2;
 if(cs){v[2]=rd32(cs+8);v[3]=rd32(cs+12);v[4]=rd64(cs+16)==(v[3]?1:0);
  u64 debug=rd64(cs);v[5]=debug&&debug!=~0ULL;v[6]=rd64(cs+24)!=0;v[7]=rd64(cs+32);n=8;}
 nt_partial_values("NT_LOCK",step,v,n);
}
static int nt_lock_test(void){
 const ram_file *cmd=find_file("cmd.exe");if(!cmd)return 0;pe_load(cmd,0);
 pe_module *m=module_named("ntdll.dll");if(!m||m->state!=2||m->size<0x16d360||!nt_syscall_init())return 0;
 const char *names[]={"RtlInitializeSListHead","RtlInterlockedPushListSList","RtlQueryDepthSList","RtlInitializeCriticalSectionEx","RtlEnterCriticalSection","RtlLeaveCriticalSection","RtlDeleteCriticalSection"};
 const u32 rvas[]={0x6fba0,0xa1210,0x6c740,0x20bc0,0x2faa0,0x2f230,0x10fc0};u64 fn[7];
 for(unsigned i=0;i<7;i++){fn[i]=module_export(m,names[i],0,0);if(fn[i]!=(u64)m->base+rvas[i])return 0;}
 u8 *head=m->base+0x16d350,*pool=m->base+0x16ae40,*list=m->base+0x166640;
 if(rd64(list)!=(u64)list||rd64(list+8)!=(u64)list||rd64(m->base+0x16d4d8)||rd64(m->base+0x16ac28))return 0;
 typedef u32(*init_t)(void*,u32,u32);typedef u32(*lock_t)(void*);typedef u16(*depth_t)(void*);
 init_t init=(init_t)fn[3];lock_t enter=(lock_t)fn[4],leave=(lock_t)fn[5],remove=(lock_t)fn[6];depth_t depth=(depth_t)fn[2];
 ((void(*)(void*))fn[0])(head);
 for(unsigned i=0;i<8;i++)pp_put64(pool+i*0x30,i==7?0:(u64)(pool+(i+1)*0x30));
 ((void*(*)(void*,void*,void*,u32))fn[1])(head,pool,pool+7*0x30,8);
 if(depth(head)!=8)return 0;
 u64 old_gs=read_msr(0xc0000101);u16 old_selector,selector=16;
 __asm__ volatile("mov %%gs,%0":"=r"(old_selector));__asm__ volatile("mov %0,%%gs"::"r"(selector):"memory");
 write_msr(0xc0000101,(u64)app_teb);
 __asm__ volatile("xor %%eax,%%eax; cpuid":::"rax","rbx","rcx","rdx","memory");
 int ok=1;u8 cs[40] __attribute__((aligned(16)));u8 live[8][48] __attribute__((aligned(16)));
 /* Shared trace gate is disabled; no scheduler or contended-lock claim. */
 for(unsigned mode=0;mode<2;mode++){
  u32 flags=mode?0x10000000:0,s;pg_zero(cs,sizeof(cs));
  s=init(cs,0,flags);nt_lock_record("initialize",flags,s,cs);if(s){ok=0;goto done;}
  if(depth(head)!=(mode?7:8)){ok=0;goto done;}
  const char *steps[]={"enter","recursive-enter","recursive-leave","leave"};
  for(unsigned i=0;i<4;i++){s=(i<2?enter:leave)(cs);nt_lock_record(steps[i],flags,s,cs);if(s){ok=0;goto done;}}
  s=remove(cs);nt_lock_record("delete",flags,s,0);if(s||depth(head)!=8){ok=0;goto done;}
  for(unsigned i=0;i<40;i++)if(cs[i]){ok=0;goto done;}
 }
 {const u32 spins[]={0x1000000,0,0},flags[]={0,0xe0000000,0x11000000};
 for(unsigned k=0;k<3;k++){
  for(unsigned i=0;i<40;i++)cs[i]=0xa5;
  u32 s=init(cs,spins[k],flags[k]);int unchanged=1;for(unsigned i=0;i<40;i++)if(cs[i]!=0xa5)unchanged=0;
  nt_partial_values("NT_LOCK","invalid",(u64[]){spins[k],flags[k],s,unchanged},4);
  if(s!=(k?0xc00000f1U:0xc00000f0U)||!unchanged){ok=0;goto done;}
 }}
 for(unsigned i=0;i<8;i++){
  pg_zero(live[i],48);if(init(live[i],0,0x10000000)||depth(head)!=7-i){ok=0;goto done;}
  u64 node=rd64(live[i]);if(node<(u64)pool||node>=(u64)pool+8*0x30||(node-(u64)pool)%0x30){ok=0;goto done;}
  for(unsigned j=0;j<i;j++)if(node==rd64(live[j])){ok=0;goto done;}
 }
 for(unsigned i=8;i;i--)if(remove(live[i-1])||depth(head)!=9-i){ok=0;goto done;}
 if(rd64(list)!=(u64)list||rd64(list+8)!=(u64)list||rd64(m->base+0x16d4d8)){ok=0;goto done;}
 print("NT_LOCK_POOL_OK: eight live debug nodes, reverse deletion, pool restored\n");
done:
 __asm__ volatile("mov %0,%%gs"::"r"(old_selector):"memory");write_msr(0xc0000101,old_gs);
 return ok&&read_msr(0xc0000101)==old_gs;
}
