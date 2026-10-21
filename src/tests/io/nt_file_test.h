struct scenario {const char *name;unsigned write,count,has_offset;int64_t offset;uint8_t payload[32];};
#include "../../../tests/fixtures/file_io_cases.h"
static void nt_file_hexbytes(const u8 *bytes,unsigned count){
 const char *digits="0123456789abcdef";for(unsigned i=0;i<count;i++){char s[3]={digits[bytes[i]>>4],digits[bytes[i]&15],0};print(s);}
}
static int nt_file_test(void){
 const ram_file *cmd=find_file("cmd.exe");if(!cmd||nt_native.module)return 0;
 pe_load(cmd,0);pe_module *m=module_named("ntdll.dll"),*k=module_named("kernelbase.dll");
 if(!k||!nt_native_start(m))return 0;
 typedef u32(*transfer_t)(void*,void*,void*,void*,void*,void*,u32,const int64_t*,const u32*);
 transfer_t read=(transfer_t)nt_checked_stub(m,"NtReadFile",6),write=(transfer_t)nt_checked_stub(m,"NtWriteFile",8);
 u64 query=nt_checked_stub(m,"NtQueryVolumeInformationFile",73),close=nt_checked_stub(m,"NtClose",15);
 typedef int(*win_transfer_t)(void*,void*,u32,u32*,void*);
 win_transfer_t wread=(win_transfer_t)module_export(k,"ReadFile",0,0),wwrite=(win_transfer_t)module_export(k,"WriteFile",0,0);
 u64 type=module_export(k,"GetFileType",0,0);
 if(!read||!write||!query||!close||!wread||!wwrite||!type)return 0;
 nt_native_scope scope;nt_native_enter(&scope);int ok=0;u64 handle=0;
 u8 data[64]={0};io_file file={data,0,sizeof(data)};
 if(io_file_open(&nt_objects,&nt_io,&file,3,&handle))goto done;
 for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);i++){
  const struct scenario *c=&cases[i];u8 iosb[16],buffer[32];int64_t offset=c->offset;
  for(unsigned j=0;j<16;j++)iosb[j]=0xa5;for(unsigned j=0;j<32;j++)buffer[j]=0xa5;
  if(c->write)for(unsigned j=0;j<c->count;j++)buffer[j]=c->payload[j];
  u32 status=(c->write?write:read)((void*)handle,0,0,0,iosb,buffer,c->count,c->has_offset?&offset:0,0);
  print("NT_FILE ");print(c->name);print(" ");number(status);print(" ");nt_file_hexbytes(iosb,16);print(" ");nt_file_hexbytes(buffer,32);print("\n");
 }
 u8 iosb[16],info[16];for(unsigned i=0;i<16;i++)iosb[i]=info[i]=0xa5;
 if(((u32(*)(void*,void*,void*,u32,u32))query)((void*)handle,iosb,info,8,4)||rd32(info)!=7||rd32(info+4)||rd32(iosb)||rd32(iosb+4)!=0xa5a5a5a5||rd64(iosb+8)!=8||!nt_info_unchanged(info,8,16))goto done;
 pp_put32(app_teb+0x68,0x1234);
 if(((u32(*)(void*))type)((void*)handle)!=1||rd32(app_teb+0x68)!=0x1234)goto done;
 /* Original synchronous Win32 wrappers: sequential write, read and EOF. */
 io_open_file *opened;if(io_file_resolve(&nt_objects,&nt_io,handle,0,&opened))goto done;
 opened->position=0;file.length=0;u8 payload[4]={'W',0,'I','O'},out[8];u32 transferred=0;
 if(!wwrite((void*)handle,payload,4,&transferred,0)||transferred!=4)goto done;
 opened->position=0;for(unsigned i=0;i<8;i++)out[i]=0xa5;
 if(!wread((void*)handle,out,8,&transferred,0)||transferred!=4)goto done;
 for(unsigned i=0;i<4;i++)if(out[i]!=payload[i])goto done;
 if(!nt_info_unchanged(out,4,8)||!wread((void*)handle,out,8,&transferred,0)||transferred)goto done;
 if(((u32(*)(void*))close)((void*)handle))goto done;
 for(unsigned i=0;i<16;i++)iosb[i]=0xa5;
 if(read((void*)handle,0,0,0,iosb,out,1,0,0)!=OBJ_INVALID_HANDLE||!nt_info_unchanged(iosb,0,16))goto done;
 handle=0;ok=1;
 done:if(handle)obj_close(&nt_objects,handle);nt_native_leave(&scope);
 if(!nt_native_dispose())return 0;
 if(ok)print("NT_FILE_WRAPPERS_OK: original ReadFile WriteFile GetFileType, EOF, close and stale handle\n");return ok;
}
