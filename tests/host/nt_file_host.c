#include <stdio.h>
#include <string.h>
#undef NDEBUG
#include <assert.h>
#include "../../src/nt/io/nt_file_core.h"
int main(void){
 obj_table objects={0};io_table io={0};uint8_t storage[64]={0};io_file file={storage,0,sizeof(storage)};
 uint64_t handle;assert(!io_file_open(&objects,&io,&file,OBJ_FILE_READ|OBJ_FILE_WRITE,&handle));
 struct scenario {const char *name,*payload;int write,has_offset;int64_t offset;uint32_t count;} cases[]={
 {"write-initial","AB\0CXYZ",1,0,0,7},{"read-eof","",0,0,0,4},
 {"read-zero-eof","",0,0,0,0},{"read-start","",0,1,0,3},
 {"read-current","",0,0,0,8},{"write-offset","12",1,1,2,2},
 {"read-current-after-write","",0,0,0,4},{"read-final","",0,1,0,16},
 {"read-zero-start","",0,1,0,0},{"read-after-zero","",0,0,0,2},
 {"write-zero-offset","",1,1,1,0},{"read-after-zero-write","",0,0,0,2},
 {"write-gap","Z",1,1,10,1},{"read-gap","",0,1,0,16}};
 for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);i++){
  struct scenario *c=&cases[i];uint8_t buffer[32];memset(buffer,0xa5,sizeof(buffer));
  if(c->write)memcpy(buffer,c->payload,c->count);
  uint64_t transferred=0xaaaa;uint32_t status=io_file_transfer(&objects,&io,handle,c->write,buffer,c->count,c->has_offset?&c->offset:0,&transferred);
  printf("{\"step\":\"%s\",\"status\":%u,\"transferred\":%llu,\"buffer\":\"",c->name,status,(unsigned long long)transferred);
  for(unsigned b=0;b<32;b++)printf("%02x",buffer[b]);puts("\"}");
 }
 uint64_t second,event,bytes=0xaaaa;uint8_t output[4]={0};int64_t offset=0;
 assert(!io_file_open(&objects,&io,&file,OBJ_FILE_READ,&second));
 assert(!io_file_transfer(&objects,&io,second,0,output,2,0,&bytes)&&bytes==2&&!memcmp(output,"AB",2));
 offset=2;output[0]='Q';assert(!io_file_transfer(&objects,&io,handle,1,output,1,&offset,&bytes));
 assert(!io_file_transfer(&objects,&io,second,0,output,1,0,&bytes)&&bytes==1&&output[0]=='Q');
 bytes=0xaaaa;assert(io_file_transfer(&objects,&io,second,1,output,2,0,&bytes)==OBJ_ACCESS_DENIED&&bytes==0xaaaa);
 assert(!obj_event_create(&objects,OBJ_EVENT_ALL,0,0,&event));
 assert(io_file_transfer(&objects,&io,event,0,output,1,0,&bytes)==OBJ_TYPE_MISMATCH);
 uint64_t old=handle;assert(!obj_close(&objects,old));
 assert(!io_file_open(&objects,&io,&file,OBJ_FILE_READ|OBJ_FILE_WRITE,&handle)&&old!=handle);
 assert(io_file_transfer(&objects,&io,old,0,output,1,0,&bytes)==OBJ_INVALID_HANDLE);
 offset=63;uint8_t saved[64];memcpy(saved,storage,64);bytes=0xaaaa;
 assert(io_file_transfer(&objects,&io,handle,1,output,2,&offset,&bytes)==OBJ_NOT_SUPPORTED);
 assert(bytes==0xaaaa&&!memcmp(saved,storage,64));
 obj_reset(&objects);assert(io_file_transfer(&objects,&io,handle,0,output,1,0,&bytes)==OBJ_INVALID_HANDLE);
 fprintf(stderr,"rights, type mismatch, stale generation, independent position, capacity rollback and reset passed\n");
 return 0;
}
