/* Original Windows startup consumers in one native context; no CRT attach. */
static void nt_ansi_record(const char *kind,unsigned index,u32 status,const nt_ansi_string *s,const u8 *data,unsigned size){
 print("NT_ANSI ");print(kind);number(index);print(" ");number(status);print(",");number(s->length);print(",");number(s->maximum);print(",");nt_file_hexbytes(data,size);print("\n");
}
static int nt_startup_test(void){
 const ram_file *cmd=find_file("cmd.exe");if(!cmd||nt_native.module)return 0;
 unsigned pages=0,regions=0;for(unsigned i=0;i<PAGE_POOL_COUNT;i++)pages+=page_used[i]!=0;
 for(unsigned i=0;i<16;i++)regions+=vm_regions[i].size!=0;
 pe_load(cmd,0);pe_module *n=module_named("ntdll.dll"),*k=module_named("kernelbase.dll");
 if(!k)return 0;
 u8 prior_basic[0x40];for(unsigned i=0;i<0x40;i++)prior_basic[i]=k->base[0x2b6d60+i];
 u8 prior_version[16],prior_command[16];for(unsigned i=0;i<16;i++){prior_version[i]=app_peb[0x118+i];prior_command[i]=k->base[0x2b6f50+i];}
 /* Force the final console stage to fail after strings/NLS were allocated.
  * The whole context must roll back, retaining these caller-owned events. */
 u64 events[OBJ_SLOTS-2];unsigned event_count=0;
 for(;event_count<OBJ_SLOTS-2;event_count++)if(obj_event_create(&nt_objects,OBJ_EVENT_ALL,0,0,&events[event_count]))break;
 int rolled_back=event_count==OBJ_SLOTS-2&&!nt_native_start(n)&&!nt_native.module&&!kb_inputs.module&&!kb_nls.module&&!nt_console.active;
 for(unsigned i=0;i<event_count;i++)if(obj_close(&nt_objects,events[i]))rolled_back=0;
 for(unsigned i=0;i<16;i++)if(app_peb[0x118+i]!=prior_version[i]||k->base[0x2b6f50+i]!=prior_command[i])rolled_back=0;
 for(unsigned i=0;i<0x40;i++)if(k->base[0x2b6d60+i]!=prior_basic[i])rolled_back=0;
 if(!rolled_back)return 0;
 print("KB_INPUTS_ROLLBACK_OK: failed console stage restored owned startup inputs\n");
 if(!nt_native_start(n))return 0;
 u32(*convert)(nt_ansi_string*,const pp_string*,u8)=(void*)module_export(n,"RtlUnicodeStringToAnsiString",0,0);
 void(*release)(nt_ansi_string*)=(void*)module_export(n,"RtlFreeAnsiString",0,0);
 int(*version)(void*)=(void*)module_export(k,"GetVersionExW",0,0);
 void(*startup)(void*)=(void*)module_export(k,"GetStartupInfoW",0,0);
 if(!convert||!release||!version||!startup)return 0;
 char*(*line)(void)=(void*)module_export(k,"GetCommandLineA",0,0);
 void*(*create_heap)(u32,u64,u64)=(void*)module_export(k,"HeapCreate",0,0);
 int(*destroy_heap)(void*)=(void*)module_export(k,"HeapDestroy",0,0);
 void*(*allocate)(void*,u32,u64)=(void*)module_export(n,"RtlAllocateHeap",0,0);
 u8(*free_block)(void*,u32,void*)=(void*)module_export(n,"RtlFreeHeap",0,0);
 if(!create_heap||!destroy_heap||!allocate||!free_block)return 0;
 if(!line||kb_inputs.module!=k)return 0;
 u32 saved_flags=rd32(app_parameters+0xa4);nt_ansi_string dst={0};
 nt_native_scope scope;nt_native_enter(&scope);int ok=0;
 void *private_heap=0,*large_block=0;
 if(rd32(k->base+0x2b6d68)!=4096)goto done;
 private_heap=create_heap(0,0x1000,0);
 if(!private_heap||rd32(app_peb+0xe8)!=2)goto done;
 /* CRT-sized initial commit, then an allocation beyond that first page. */
 large_block=allocate(private_heap,8,65536);
 if(!large_block||!nt_obj_readable(large_block,65536))goto done;
 for(unsigned i=0;i<65536;i++)if(((u8*)large_block)[i])goto done;
 ((u8*)large_block)[65535]=0x5a;
 nt_native_leave(&scope);nt_native_enter(&scope);
 if(((u8*)large_block)[65535]!=0x5a||rd32(k->base+0x2b6d68)!=4096)goto done;
 if(!free_block(private_heap,0,large_block))goto done;large_block=0;
 /* Isolated, version-pinned probe: original NTDLL heap-policy branch. */
 u32(*set_policy)(void*,u32,const u32*,u64)=(void*)module_export(n,"RtlSetHeapInformation",0,0);
 if(!set_policy)goto done;
 u32 policy=2;
 u32 short_status=set_policy(private_heap,0,&policy,0);
 policy=1;u32 bad_status=set_policy(private_heap,0,&policy,sizeof(policy));
 policy=2;u32 good_status=set_policy(private_heap,0,&policy,sizeof(policy));
 print("NT_HEAP_POLICY_PROBE statuses(short,bad,good): ");print_hex(short_status);
 print(" ");print_hex(bad_status);print(" ");print_hex(good_status);print("\n");
 if(short_status!=0xc0000023U||bad_status!=0xc0000001U||good_status)goto done;
 if(((u8*)private_heap)[0x1a3]!=2)goto done; /* Pinned NTDLL internal state. */
 if(set_policy(private_heap,0,&policy,sizeof(policy)))goto done;
 print("NT_HEAP_POLICY_OK: NTDLL policy 2, invalid paths, repeat and ownership\n");
 if(!destroy_heap(private_heap))goto done;private_heap=0;
 if(rd32(app_peb+0xe8)!=1)goto done;
 print("KB_PRIVATE_HEAP_OK: original create/destroy, 64 KiB zero allocation, persistent system info\n");
 const u16 *texts[]={u"cmd.exe /d /c echo Winux",u"a\u00e7\u00e3o",u"\u20ac",u"\u4e2d",u"A\0B",u""};
 const unsigned lengths[]={24,4,1,1,3,0};
 for(unsigned i=0;i<6;i++){
  pp_string src={(u16)(lengths[i]*2),(u16)(lengths[i]*2+2),texts[i]};
  u32 status=convert(&dst,&src,1);
  if(status||!dst.data||dst.length!=lengths[i]||dst.maximum!=lengths[i]+1||!nt_obj_readable(dst.data,dst.maximum))goto done;
  nt_ansi_record("alloc",i,status,&dst,dst.data,dst.length+1);
  release(&dst);if(dst.length||dst.maximum||dst.data)goto done;
 }
 for(unsigned i=0;i<4;i++){
  const unsigned caps[]={0,1,3,4};u8 buffer[8];for(unsigned j=0;j<8;j++)buffer[j]=0xa5;
  pp_string src={6,8,u"ABC"};nt_ansi_string local={0,(u16)caps[i],0,buffer};
  u32 status=convert(&local,&src,0);if(local.data!=buffer)goto done;
  nt_ansi_record("cap",caps[i],status,&local,buffer,8);
 }
 print("NT_ANSI_FREE_OK\n");
 /* Explicit synthetic compatibility profile, not a claim about our kernel.
  * 0x114 is the basic structure used by the pinned CRT first initializer. */
 if(rd64(app_peb+0x2d8)||rd64(app_peb+0x2f0))goto done;
 if(kb_inputs_start(n))goto done; /* Reject duplicate ownership. */
 for(unsigned call=0;call<2;call++){
  nt_native_leave(&scope);nt_native_enter(&scope);
  char *text=line();if(text!=(char*)kb_inputs.command.data||!text)goto done;
  for(unsigned i=0;i<=kb_inputs.command.length;i++)if((u8)text[i]!=app_cmdline[i])goto done;
  u8 info[0x124];for(unsigned i=0;i<sizeof(info);i++)info[i]=0xa5;pp_put32(info,0x114);
  if(!version(info)||rd32(info+4)!=10||rd32(info+8)||rd32(info+12)!=19041||rd32(info+16)!=2||rd16(info+20))goto done;
  if(!nt_info_unchanged(info,0x114,sizeof(info))||rd64(k->base+0x2b8558)!=(u64)k->base+0x81290)goto done;
 }
 print("KB_INPUTS_PERSISTENT_OK: original command line and version across scopes, duplicate rejected\n");
 print("KB_VERSION_BASIC_OK: declared 10.0.19041, platform 2, original selector and cached target\n");
 for(unsigned mode=0;mode<2;mode++){
  u8 info[0x70];for(unsigned i=0;i<sizeof(info);i++)info[i]=0xa5;
  pp_put32(app_parameters+0xa4,mode?0x100:0);startup(info);
  if(rd32(info)!=0x68||rd32(info+0x3c)!=(mode?0x100:0)||!nt_info_unchanged(info,0x68,sizeof(info)))goto done;
  const unsigned fields[]={0xd8,0xc8,0xb8};
  for(unsigned i=0;i<3;i++)if(rd64(info+8+i*8)!=rd64(app_parameters+fields[i]))goto done;
  for(unsigned i=0;i<7;i++)if(rd32(info+0x20+i*4)!=rd32(app_parameters+0x88+i*4))goto done;
  if(rd16(info+0x40)!=rd16(app_parameters+0xa8)||rd16(info+0x42)!=rd16(app_parameters+0xe0)||rd64(info+0x48)!=rd64(app_parameters+0xe8))goto done;
  if(mode){for(unsigned i=0;i<3;i++)if(rd64(info+0x50+i*8)!=nt_console.handles[i])goto done;}
  else if(!nt_info_unchanged(info,0x50,0x68))goto done;
 }
 print("KB_STARTUP_INFO_OK: copied fields, conditional standard handles, canaries\n");ok=1;
 done:
 if(large_block&&private_heap)free_block(private_heap,0,large_block);
 if(private_heap&&!destroy_heap(private_heap))ok=0;
 if(dst.data)release(&dst);
 pp_put32(app_parameters+0xa4,saved_flags);
 nt_native_leave(&scope);if(!nt_native_dispose())return 0;
 if(kb_inputs.module)return 0;
 for(unsigned i=0;i<16;i++)if(app_peb[0x118+i]!=prior_version[i]||k->base[0x2b6f50+i]!=prior_command[i])return 0;
 for(unsigned i=0;i<0x40;i++)if(k->base[0x2b6d60+i]!=prior_basic[i])return 0;
 unsigned after=0,left=0;for(unsigned i=0;i<PAGE_POOL_COUNT;i++)after+=page_used[i]!=0;
 for(unsigned i=0;i<16;i++)left+=vm_regions[i].size!=0;
 return ok&&after==pages&&left==regions;
}
