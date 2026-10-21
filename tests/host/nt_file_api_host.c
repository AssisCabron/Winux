#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/nt/io/nt_file_core.h"
static obj_table nt_objects;
static io_table nt_io;
static union {uint64_t align;uint8_t bytes[256];} arena;
static int nt_obj_readable(const void *p,uint64_t n){uintptr_t a=(uintptr_t)p,b=(uintptr_t)arena.bytes;return a>=b&&a-b<256&&n<=256-(a-b);}
static int nt_obj_writable(const void *p,uint64_t n){return nt_obj_readable(p,n)&&!((uintptr_t)p<(uintptr_t)arena.bytes+240&&(uintptr_t)p+n>(uintptr_t)arena.bytes+224);}
#include "../../src/nt/io/nt_file_api.h"
static void check(int ok){if(!ok){fprintf(stderr,"adapter invariant failed\n");exit(1);}}
static uint32_t empty_endpoint(void *context,void *buffer,uint32_t count,uint64_t *done){
 (void)context;(void)buffer;(void)count;(void)done;return UINT32_MAX;
}
struct scenario {const char *name;unsigned write,count,has_offset;int64_t offset;uint8_t payload[32];};
#include "../fixtures/file_io_cases.h"
int main(void){
 uint8_t data[64]={0};io_file file={data,0,64};uint64_t handle;
 check(!io_file_open(&nt_objects,&nt_io,&file,3,&handle));
 void *h=(void*)(uintptr_t)handle;uint8_t *iosb=arena.bytes,*buffer=arena.bytes+32;
 int64_t *offset=(int64_t*)(arena.bytes+80);
 for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);i++){
  const struct scenario *c=&cases[i];memset(iosb,0xa5,16);memset(buffer,0xa5,32);
  if(c->write)memcpy(buffer,c->payload,c->count);*offset=c->offset;
  uint32_t status=c->write?nt_WriteFile(h,0,0,0,iosb,buffer,c->count,c->has_offset?offset:0,0):nt_ReadFile(h,0,0,0,iosb,buffer,c->count,c->has_offset?offset:0,0);
  printf("{\"step\":\"%s\",\"status\":%u,\"iosb\":\"",c->name,status);
  for(unsigned b=0;b<16;b++)printf("%02x",iosb[b]);printf("\",\"buffer\":\"");
  for(unsigned b=0;b<32;b++)printf("%02x",buffer[b]);puts("\"}");
 }
 io_open_file *open;check(!io_file_resolve(&nt_objects,&nt_io,handle,0,&open));
 uint64_t position=open->position,length=file.length;uint8_t saved[64];memcpy(saved,data,64);
 memset(iosb,0xa5,16);*offset=0;
 check(nt_WriteFile(h,0,0,0,(void*)1,buffer,2,offset,0)==0xc0000005U);
 check(nt_WriteFile(h,0,0,0,iosb,arena.bytes+255,2,offset,0)==0xc0000005U);
 check(nt_ReadFile(h,0,0,0,iosb,arena.bytes+224,2,offset,0)==0xc0000005U);
 check(nt_ReadFile(h,0,0,0,iosb,buffer,2,(int64_t*)1,0)==0xc0000005U);
 check(nt_WriteFile(h,(void*)1,0,0,iosb,buffer,2,offset,0)==OBJ_NOT_SUPPORTED);
 check(nt_ReadFile(h,0,(void*)1,0,iosb,buffer,2,offset,0)==OBJ_NOT_SUPPORTED);
 check(open->position==position&&file.length==length&&!memcmp(saved,data,64));
 for(unsigned i=0;i<16;i++)check(iosb[i]==0xa5);
 memset(buffer,0xa5,24);
 check(nt_QueryVolumeInformationFile(0,iosb,buffer,7,4)==0xc0000004U);
 check(!nt_QueryVolumeInformationFile(h,iosb,buffer,8,4));
 check(buffer[0]==7&&buffer[4]==0&&iosb[0]==0&&iosb[4]==0xa5&&iosb[8]==8);
 for(unsigned i=8;i<24;i++)check(buffer[i]==0xa5);
 uint64_t event;check(!obj_event_create(&nt_objects,OBJ_EVENT_ALL,0,0,&event));
 check(nt_QueryVolumeInformationFile((void*)(uintptr_t)event,iosb,buffer,8,4)==OBJ_TYPE_MISMATCH);
 check(!obj_close(&nt_objects,handle));
 check(nt_ReadFile(h,0,0,0,iosb,buffer,2,offset,0)==OBJ_INVALID_HANDLE);
 check(nt_QueryVolumeInformationFile(h,iosb,buffer,8,4)==OBJ_INVALID_HANDLE);
 io_endpoint endpoint={0,empty_endpoint,0,0x50,0};uint64_t console;
 check(!io_endpoint_open(&nt_objects,&nt_io,&endpoint,OBJ_FILE_READ,&console));
 memset(iosb,0xa5,16);
 check(nt_ReadFile((void*)(uintptr_t)console,0,0,0,iosb,buffer,2,0,0)==OBJ_NOT_SUPPORTED);
 for(unsigned i=0;i<16;i++)check(iosb[i]==0xa5);
 check(!nt_QueryVolumeInformationFile((void*)(uintptr_t)console,iosb,buffer,8,4)&&buffer[0]==0x50);
 fprintf(stderr,"output validation, read-only and crossing ranges, rollback, async rejection, device query, type and stale handle passed\n");
 return 0;
}
