#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/nt/io/nt_file_core.h"
#include "../../src/console/console_line.h"
static void check(int ok){if(!ok){fputs("console invariant failed\n",stderr);exit(1);}}
typedef struct {console_line input;uint8_t output[128];unsigned used;} fixture;
static uint32_t read_input(void *context,void *buffer,uint32_t count,uint64_t *done){
 return console_line_read(&((fixture*)context)->input,buffer,count,done);
}
static uint32_t write_output(void *context,void *buffer,uint32_t count,uint64_t *done){
 fixture *f=context;if(count>sizeof(f->output)-f->used)return OBJ_NOT_SUPPORTED;
 memcpy(f->output+f->used,buffer,count);f->used+=count;*done=count;return 0;
}
int main(void){
 obj_table objects={0};io_table io={0};fixture f={0};
 io_endpoint endpoint={&f,read_input,write_output,0x50,0};uint64_t in,out,error,done=0xaaaa;
 check(!io_endpoint_open(&objects,&io,&endpoint,OBJ_FILE_READ,&in));
 check(!io_endpoint_open(&objects,&io,&endpoint,OBJ_FILE_WRITE,&out));
 check(!io_endpoint_open(&objects,&io,&endpoint,OBJ_FILE_WRITE,&error));
 uint8_t buffer[300];memset(buffer,0xa5,sizeof(buffer));
 check(io_file_transfer(&objects,&io,in,0,buffer,4,0,&done)==CONSOLE_NEED_INPUT&&done==0xaaaa&&buffer[0]==0xa5);
 check(!io_file_transfer(&objects,&io,in,0,0,0,0,&done)&&done==0);
 check(console_line_feed(&f.input,'a')==1&&console_line_feed(&f.input,'x')==1);
 check(console_line_feed(&f.input,8)==2&&console_line_feed(&f.input,'b')==1);
 check(console_line_feed(&f.input,'\r')==3&&console_line_feed(&f.input,'z')==0);
 check(!io_file_transfer(&objects,&io,in,0,buffer,1,0,&done)&&done==1&&buffer[0]=='a');
 check(!io_file_transfer(&objects,&io,in,0,buffer,8,0,&done)&&done==3&&!memcmp(buffer,"b\r\n",3));
 check(console_line_feed(&f.input,'\n')==0&&!f.input.ready);
 check(console_line_feed(&f.input,'\n')==3);
 check(!io_file_transfer(&objects,&io,in,0,buffer,8,0,&done)&&done==2&&!memcmp(buffer,"\r\n",2));
 for(unsigned i=0;i<300;i++)console_line_feed(&f.input,'q');
 check(f.input.length==254&&console_line_feed(&f.input,'\n')==3);
 check(!io_file_transfer(&objects,&io,in,0,buffer,300,0,&done)&&done==256&&buffer[254]=='\r'&&buffer[255]=='\n');
 uint8_t text[]={'A',0,'B'};check(!io_file_transfer(&objects,&io,out,1,text,3,0,&done)&&done==3);
 check(!memcmp(f.output,text,3));
 check(io_file_transfer(&objects,&io,out,0,buffer,1,0,&done)==OBJ_ACCESS_DENIED);
 check(io_file_transfer(&objects,&io,in,1,text,3,0,&done)==OBJ_ACCESS_DENIED);
 int64_t offset=0;check(io_file_transfer(&objects,&io,out,1,text,3,&offset,&done)==OBJ_NOT_SUPPORTED);
 check(!obj_close(&objects,out));check(io_file_transfer(&objects,&io,out,1,text,3,0,&done)==OBJ_INVALID_HANDLE);
 check(!io_file_transfer(&objects,&io,error,1,text,3,0,&done)&&f.used==6);
 check(!obj_close(&objects,error)&&!obj_close(&objects,in));
 uint64_t again;check(!io_endpoint_open(&objects,&io,&endpoint,OBJ_FILE_READ,&again)&&again!=in);
 check(io_file_transfer(&objects,&io,in,0,buffer,1,0,&done)==OBJ_INVALID_HANDLE);
 obj_reset(&objects);check(io_file_transfer(&objects,&io,again,0,buffer,1,0,&done)==OBJ_INVALID_HANDLE);
 puts("line editing, partial reads, CRLF, capacity, NUL output, rights, shared endpoint, close and reset passed");return 0;
}
