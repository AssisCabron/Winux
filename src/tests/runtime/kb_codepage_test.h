/* CRT environment boundary: original copy/free plus ACP conversion. */
static int kb_environment_test(pe_module *k){
 u16*(*copy)(void)=(u16*(*)(void))module_export(k,"GetEnvironmentStringsW",0,0);
 int(*release)(void*)=(int(*)(void*))module_export(k,"FreeEnvironmentStringsW",0,0);
 int(*convert)(u32,u32,const u16*,int,void*,int,const void*,void*)=(void*)module_export(k,"WideCharToMultiByte",0,0);
 if(!copy||!release||!convert||app_environment_bytes<4||app_environment_bytes>1024)return 0;
 /* EnvironmentSize is padded; the copied block ends at the first double
  * NUL. Do not read allocator padding beyond the original copy contract. */
 u64 units=0;while(units+1<app_environment_bytes/2){if(!app_environment[units]&&!app_environment[units+1]){units+=2;break;}units++;}
 if(units<2||units>app_environment_bytes/2||app_environment[units-1]||app_environment[units-2])return 0;
 u16 *first=copy(),*second=copy();int ok=0;u8 ansi[512];
 if(!first||!second||first==second||first==app_environment||second==app_environment)goto done;
 if(!nt_obj_readable(first,units*2)||!nt_obj_readable(second,units*2))goto done;
 for(u64 i=0;i<units;i++)if(first[i]!=app_environment[i]||second[i]!=app_environment[i])goto done;
 if(first[units-1]||first[units-2])goto done;
 if(convert(0,0,second,(int)units,0,0,0,0)!=(int)units||convert(0,0,second,(int)units,ansi,sizeof(ansi),0,0)!=(int)units)goto done;
 for(u64 i=0;i<units;i++)if(app_environment[i]>127||ansi[i]!=(u8)app_environment[i])goto done;
 first[0]^=1;if(second[0]!=app_environment[0])goto done;
 ok=1;
 done:
 if(second&&!release(second))ok=0;if(first&&!release(first))ok=0;
 if(ok)print("KB_ENVIRONMENT_OK: independent original copies, ACP block with NULs, free\n");return ok;
}
/* Pinned KernelBase NLS consumers, no DLL entry point or locale bootstrap. */
static int kb_codepage_test(void){
 const ram_file *cmd=find_file("cmd.exe");if(!cmd||nt_native.module)return 0;
 unsigned before=0,regions=0;
 for(unsigned i=0;i<PAGE_POOL_COUNT;i++)before+=page_used[i]!=0;
 for(unsigned i=0;i<16;i++)regions+=vm_regions[i].size!=0;
 pe_load(cmd,0);pe_module *n=module_named("ntdll.dll"),*k=module_named("kernelbase.dll");
 if(!k||k->state!=2||k->size<0x2b88a8||!nt_native_start(n))return 0;
 typedef int(*convert_t)(u32,u32,const u16*,int,void*,int,const void*,void*);
 typedef int(*info_t)(u32,void*);typedef int(*valid_t)(u32);
 convert_t convert=(convert_t)module_export(k,"WideCharToMultiByte",0,0);
 info_t info=(info_t)module_export(k,"GetCPInfo",0,0);
 valid_t valid=(valid_t)module_export(k,"IsValidCodePage",0,0);
 if((u64)convert!=(u64)k->base+0x4bd20||(u64)info!=(u64)k->base+0x4d1e0||(u64)valid!=(u64)k->base+0x4ce40)return 0;
 if(kb_nls.module!=k||!kb_nls.acp||!kb_nls.oem)return 0;
 u64 acp=kb_nls.acp,oem=kb_nls.oem;
 nt_native_scope scope;nt_native_enter(&scope);int ok=0;
 if(kb_nls_start())goto done; /* Duplicate ownership must be rejected. */
 if(rd32(k->base+0x2b8200)!=1252||rd32(k->base+0x2b8204)!=850)goto done;
 static const u16 strings[][24]={{'c','m','d','.','e','x','e',' ','/','c',' ','e','c','h','o',' ','W','i','n','u','x',0},
 {'a',0xe7,0xe3,'o',' ',0xc1,0xc9,0},{0x20ac,0},{0x4e2d,0},{'A',0,'B',0},{'A','B','C',0}};
 const char *labels[]={"ascii","portuguese","euro","unmappable","embedded-nul","terminated"};
 int lengths[]={21,7,1,1,3,-1};
 for(unsigned p=0;p<2;p++){
  nt_native_leave(&scope);nt_native_enter(&scope);
  if(kb_nls.acp!=acp||kb_nls.oem!=oem||rd64(k->base+0x2b8880)!=acp||rd64(k->base+0x2b88a0)!=oem)goto done;
  u32 cp=p?850:1252;u8 cpinfo[20]={0};
  if(!info(cp,cpinfo))goto done;
  u64 fields[17]={cp,(u64)(valid(cp)!=0),rd32(cpinfo),cpinfo[4],cpinfo[5]};
  for(unsigned i=0;i<12;i++)fields[5+i]=cpinfo[6+i];
  nt_partial_values("KB_CP","cpinfo",fields,17);
  for(unsigned j=0;j<6;j++){
   u8 out[64];for(unsigned i=0;i<64;i++)out[i]=0xa5;
   int size=convert(cp,0,strings[j],lengths[j],0,0,0,0);
   if(size<=0||size>32)goto done;
   int written=convert(cp,0,strings[j],lengths[j],out,size,0,0);
   if(written!=size||out[size]!=0xa5)goto done;
   u64 values[34]={cp,(u64)written};for(int i=0;i<written;i++)values[i+2]=out[i];
   nt_partial_values("KB_CP",labels[j],values,written+2);
   /* CP_ACP/CP_OEMCP must select the same nodes as explicit IDs. */
   u8 alias[64];if(convert(p?1:0,0,strings[j],lengths[j],alias,size,0,0)!=size)goto done;
   for(int i=0;i<size;i++)if(alias[i]!=out[i])goto done;
   if(j<5&&size>1){
    pp_put32(app_teb+0x68,0);int r=convert(cp,0,strings[j],lengths[j],out,size-1,0,0);
    u32 error=rd32(app_teb+0x68);
    print("KB_CP ");print(labels[j]);print("-short ");number(cp);print(",");number(r);print(",");number(error);print("\n");
    if(r||error!=122)goto done;
   }
  }
 }
 if(!kb_environment_test(k))goto done;
 ok=1;
 done:
 nt_native_leave(&scope);if(!nt_native_dispose())return 0;
 if(kb_nls.module||rd64(k->base+0x2b8880)||rd64(k->base+0x2b88a0))return 0;
 if(ok)print("KB_NLS_PERSISTENT_OK: original nodes survive scopes, duplicate rejected, context cleanup\n");
 unsigned after=0,remaining=0;
 for(unsigned i=0;i<PAGE_POOL_COUNT;i++)after+=page_used[i]!=0;
 for(unsigned i=0;i<16;i++)remaining+=vm_regions[i].size!=0;
 if(!ok||before!=after||regions!=remaining)return 0;
 print("KB_CP_TEARDOWN_OK: nodes released before heap, aliases matched, resources restored\n");return 1;
}
