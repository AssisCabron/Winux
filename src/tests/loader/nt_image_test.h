/* Current-process image metadata and the original address-to-image consumer. */
static int nt_image_test(void){
 const ram_file *cmd=find_file("cmd.exe");if(!cmd||nt_native.module)return 0;
 pe_load(cmd,0);pe_module *m=module_named("ntdll.dll");if(!nt_native_start(m))return 0;
 typedef u32(*query_t)(void*,void*,u32,void*,u64,u64*);
 typedef void*(*pc_t)(void*,void**);
 query_t query=(query_t)nt_checked_stub(m,"NtQueryVirtualMemory",35);
 pc_t pc=(pc_t)module_export(m,"RtlPcToFileHeader",0,0);
 if(!query||(u64)pc!=(u64)m->base+0x34a40||rd32(m->base+0x16d198)==3||rd64(m->base+0x181518))return 0;
 nt_native_scope scope;nt_native_enter(&scope);int ok=0;u8 owned[64]={0};
 void *addresses[]={m->base,m->base+16,owned,0};
 const char *labels[]={"image-base","image-interior","private-buffer","null"};
 u64 sizes[]={0,23,24,32};
 for(unsigned i=0;i<4;i++)for(unsigned j=0;j<4;j++){
  u8 output[40];for(unsigned b=0;b<40;b++)output[b]=0xa5;
  u64 returned=0xaaaa;u32 s=query((void*)~0ULL,addresses[i],6,output,sizes[j],&returned);
  u64 values[10]={sizes[j],s,returned,(u64)nt_info_unchanged(output,0,40)};unsigned count=4;
  if(!s){values[4]=rd64(output)==(u64)m->base;values[5]=rd64(output)==0;values[6]=rd64(output+8);
   values[7]=rd32(output+16)&3;values[8]=rd32(output+20)==0;count=9;}
  nt_partial_values("NT_IMAGE",labels[i],values,count);
  if(!nt_info_unchanged(output,24,40))goto done;
 }
 const char *names[]={"ntdll.dll","kernelbase.dll","msvcrt.dll"};
 const char *shortnames[]={"ntdll","kernelbase","msvcrt"};
 const char *points[]={"base","dos-interior","last-image-byte"};
 for(unsigned i=0;i<3;i++){
  pe_module *image=module_named(names[i]);if(!image||image->state!=2)goto done;
  u64 offsets[]={0,16,image->size-1};
  for(unsigned j=0;j<3;j++){
   void *out=(void*)0xaaaa,*result=pc(image->base+offsets[j],&out);
   print("NT_PC ");print(shortnames[i]);print(" ");print(points[j]);print(" ");
   number(result==image->base);print(",");number(out==image->base);print("\n");
   if(result!=image->base||out!=image->base)goto done;
  }
 }
 for(unsigned i=0;i<2;i++){
  void *out=(void*)0xaaaa,*result=pc(i?owned:0,&out);
  nt_partial_values("NT_PC",i?"private-buffer":"null",(u64[]){(u64)result,(u64)out},2);
  if(result||out)goto done;
 }
 /* All mapped modules, including ones absent from the host fixture. */
 for(unsigned i=0;i<module_count;i++){
  void *out=0;if(pc(modules[i].base,&out)!=modules[i].base||out!=modules[i].base)goto done;
 }
 /* Failure outputs and private reserved memory are independent guest checks. */
 u8 bad[24];for(unsigned i=0;i<24;i++)bad[i]=0xa5;
 if(query((void*)~0ULL,m->base,6,bad,24,(u64*)1)!=0xc0000005U||!nt_info_unchanged(bad,0,24))goto done;
 u64 private=vm_reserve(4096);if(!private)goto done;
 u64 ret=0;u32 s=query((void*)~0ULL,(void*)private,6,bad,24,&ret);
 int clean=s==0&&ret==24;for(unsigned i=0;i<24;i++)if(bad[i])clean=0;
 if(!vm_release(private)||!clean)goto done;
 if(query((void*)~0ULL,(void*)private,6,bad,24,&ret)!=0xc0000141U)goto done;
 ok=1;
 done:nt_native_leave(&scope);if(!nt_native_dispose())return 0;
 if(ok)print("NT_IMAGE_LIFECYCLE_OK: all modules recognized, reserved/private and invalid outputs checked\n");return ok;
}
