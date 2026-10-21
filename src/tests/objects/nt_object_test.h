/* Original stubs, real SYSCALL dispatch; namespace is an explicit test fixture. */
static void nt_object_record(u32 status,int has_previous,int32_t previous){
 u64 v[]={status,(u32)previous};nt_partial_values("NT_OBJECT","event",v,has_previous?2:1);
}
static int nt_object_test(void){
 const ram_file *cmd=find_file("cmd.exe");if(!cmd||!find_file("ntdll.dll"))return 0;
 pe_load(cmd,0);pe_module *m=module_named("ntdll.dll");if(!m||m->state!=2)return 0;
 typedef u32(*create_t)(void**,u32,nt_obj_attributes*,u32,u8);
 typedef u32(*set_t)(void*,int32_t*);typedef u32(*wait_t)(void*,u8,const int64_t*);
 typedef u32(*close_t)(void*);typedef u32(*open_t)(void**,u32,nt_obj_attributes*);
 typedef u32(*query_t)(void*,nt_obj_string*,u32*);
 create_t create=(create_t)nt_checked_stub(m,"NtCreateEvent",72);
 set_t set=(set_t)nt_checked_stub(m,"NtSetEvent",14);
 wait_t wait=(wait_t)nt_checked_stub(m,"NtWaitForSingleObject",4);
 close_t close=(close_t)nt_checked_stub(m,"NtClose",15);
 open_t directory=(open_t)nt_checked_stub(m,"NtOpenDirectoryObject",88),link=(open_t)nt_checked_stub(m,"NtOpenSymbolicLinkObject",302);
 query_t query=(query_t)nt_checked_stub(m,"NtQuerySymbolicLinkObject",351);
 if(!create||!set||!wait||!close||!directory||!link||!query||!nt_syscall_init())return 0;
 u32 before=nt_syscall_counts[7];int ok=1;int64_t zero=0;nt_syscall_allowed=1;
 for(unsigned kind=0;kind<2;kind++)for(unsigned initial=0;initial<2;initial++){
  void *h=0;u32 s=create(&h,0x100003,0,kind,(u8)initial);nt_object_record(s,0,0);
  if(s){ok=0;goto done;}
  for(unsigned phase=0;phase<2;phase++){
   if(phase)for(unsigned repeat=0;repeat<2;repeat++){int32_t previous=-999;s=set(h,&previous);nt_object_record(s,1,previous);}
   for(unsigned repeat=0;repeat<2;repeat++)nt_object_record(wait(h,0,&zero),0,0);
  }nt_object_record(close(h),0,0);
 }
 nt_object_record(wait(0,0,&zero),0,0);nt_object_record(set(0,0),0,0);nt_object_record(close(0),0,0);
 {
  u16 path[64],namebuf[64],buffer[260];ascii_wide(path,"C:\\Windows\\System32",64);
  if(obj_configure_dll_path(&nt_objects,path,38)){ok=0;goto done;}
  nt_obj_string name={20,22,0,namebuf};ascii_wide(namebuf,"\\KnownDlls",64);
  nt_obj_attributes a={48,0,0,&name,0x40,0,0,0};void *root=0,*h=0;u32 s=directory(&root,3,&a);
  nt_partial_values("NT_OBJECT","namespace",(u64[]){s},1);if(s){ok=0;goto done;}
  a.root=root;ascii_wide(namebuf,"KnownDllPath",64);name.length=24;name.maximum=26;s=link(&h,1,&a);
  nt_partial_values("NT_OBJECT","namespace",(u64[]){s},1);if(s){ok=0;goto done;}
  const unsigned capacities[]={0,2,38,39,40,48,512};
  for(unsigned i=0;i<7;i++){
   for(unsigned j=0;j<260;j++)buffer[j]=0xa5a5;
   nt_obj_string target={0,(u16)capacities[i],0,buffer};u32 required=0xffffffff;s=query(h,&target,&required);
   int match=1;if(!s){if(target.length!=38)match=0;for(unsigned j=0;j<20;j++)if(buffer[j]!=path[j])match=0;}
   nt_partial_values("NT_OBJECT","namespace",(u64[]){s,target.length,required,!s&&match},4);
   if((!s&&!match)||buffer[20]!=0xa5a5)ok=0;
  }
  ascii_wide(namebuf,"Winux_Probe_Missing_Link_6c82791a",64);name.length=62;name.maximum=64;void *missing=0;
  nt_partial_values("NT_OBJECT","namespace",(u64[]){link(&missing,1,&a)},1);
  nt_partial_values("NT_OBJECT","namespace",(u64[]){close(h)},1);
  nt_partial_values("NT_OBJECT","namespace",(u64[]){close(root)},1);
 }
 if(nt_syscall_counts[7]-before!=47)ok=0;
done:
 nt_syscall_allowed=0;obj_reset(&nt_objects);nt_objects.dll_path_bytes=0;
 return ok;
}
