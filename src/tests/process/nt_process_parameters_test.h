/* Consumer of a published process heap. Inputs are bounded synthetic fixtures. */
static int nt_process_parameters_test(pe_module *m,void *heap){
 typedef u32(*create_t)(void**,const pp_string*,const pp_string*,const pp_string*,const pp_string*,const void*,const pp_string*,const pp_string*,const pp_string*,const pp_string*,u32);
 u64 c=module_export(m,"RtlCreateProcessParametersEx",0,0),d=module_export(m,"RtlDestroyProcessParameters",0,0),n=module_export(m,"RtlNormalizeProcessParams",0,0);
 u64 previous_heap=rd64(app_peb+0x30);
 if(!c||!d||!n||(previous_heap&&previous_heap!=(u64)heap))return 0;
 const u16 *values[]={u"C:\\WinuxTest\\cmd.exe",u"C:\\WinuxTest",u"C:\\WinuxTest\\",u"cmd.exe /c echo WinuxFixture",u"WinuxFixture",u"WinSta0\\Default",u"",u""};
 const u16 env[]=u"WINUX_FIXTURE=1\0WINUX_TEXT=hello\0";
 pp_string strings[8];for(unsigned i=0;i<8;i++){unsigned units=0;while(values[i][units])units++;strings[i]=(pp_string){units*2,units*2+2,values[i]};}
 create_t create=(create_t)c;void(*destroy)(void*)=(void(*)(void*))d;void*(*normalize)(void*)=(void*(*)(void*))n;
 u64 original_parameters=rd64(app_peb+0x20);void *out=0;int ok=0;
 pp_put64(app_peb+0x30,(u64)heap);
 for(unsigned flags=0;flags<2;flags++){
  out=0;u32 status=create(&out,strings,strings+1,strings+2,strings+3,env,strings+4,strings+5,strings+6,strings+7,flags);
  if(status||!out||!nt_obj_readable(out,0x440))goto done;
  u8 *p=out;u32 maximum=rd32(p),length=rd32(p+4);
  if(length<0x440||length>maximum||maximum>8192||!nt_obj_readable(p,maximum)||(rd32(p+8)&1)!=flags)goto done;
  const u32 offsets[]={0x38,0x50,0x60,0x70,0xb0,0xc0,0xd0,0xe0};
  const unsigned indices[]={2,1,0,3,4,5,6,7};u64 prior[8];
  for(unsigned i=0;i<8;i++)prior[i]=rd64(p+offsets[i]+8);
  if(normalize(p)!=p)goto done;
  static u8 snapshot[8192]; /* Single-thread diagnostic scratch, not kernel stack. */for(unsigned i=0;i<length;i++)snapshot[i]=p[i];
  if(normalize(p)!=p)goto done;
  for(unsigned i=0;i<length;i++)if(snapshot[i]!=p[i])goto done;
  nt_partial_values("NT_PROCESS_PARAMS","header",(u64[]){flags,maximum,length,(rd32(p+8)&1)!=0,1},5);
  for(unsigned i=0;i<8;i++){
   u32 offset=offsets[i];u16 count=rd16(p+offset),capacity=rd16(p+offset+2);u64 pointer=rd64(p+offset+8);
   if(count!=strings[indices[i]].length||count>capacity)goto done;
   if(pointer){if(pointer!=(flags?prior[i]:prior[i]+(u64)p)||pointer<(u64)p||!range(pointer-(u64)p,capacity,maximum))goto done;}
   else if(count||prior[i])goto done;
   for(unsigned k=0;k<count/2;k++)if(((u16*)pointer)[k]!=values[indices[i]][k])goto done;
   nt_partial_values("NT_PROCESS_PARAMS","string",(u64[]){flags,offset,count,capacity,pointer?pointer-(u64)p:0},5);
  }
  u64 ep=rd64(p+0x80);if(!ep||!nt_obj_readable((void*)ep,sizeof(env)))goto done;
  for(unsigned i=0;i<sizeof(env)/2;i++)if(((u16*)ep)[i]!=env[i])goto done;
  nt_partial_values("NT_PROCESS_PARAMS","environment",(u64[]){flags,1,rd64(p+0x3f0)},3);
  destroy(p);out=0;
 }
 for(unsigned k=0;k<3;k++){
  pp_string invalid=strings[0];invalid.length=4;invalid.maximum=2;
  out=0;u32 status=create(&out,k==1?&invalid:strings,strings+1,k==2?strings+6:strings+2,strings+3,env,strings+4,strings+5,strings+6,strings+7,k==0?2:1);
  const char *labels[]={"unknown-flags","length-exceeds-maximum","empty-current-directory"};
  nt_partial_values("NT_PROCESS_PARAMS",labels[k],(u64[]){status,out==0},2);
  if(status!=0xc000000dU||out)goto done;
 }
 ok=rd64(app_peb+0x20)==original_parameters&&rd64(app_peb+0x30)==(u64)heap;
 done:
 if(out)destroy(out);
 /* Temporarily publish a private heap; this fixture does not launch a process. */
 pp_put64(app_peb+0x30,previous_heap);
 if(ok)print("NT_PROCESS_PARAMS_OK: original create/normalize/destroy, published heap, two layouts and three rejections\n");
 return ok;
}

