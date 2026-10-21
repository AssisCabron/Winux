#include <stdio.h>
#include <stdlib.h>
#include "../../src/nt/objects/nt_objects.h"
static obj_table table;
static void check(int ok,const char *what){if(!ok){fprintf(stderr,"FAIL: %s\n",what);exit(1);}}
static void record(const char *c,const char *op,uint32_t s,int has_previous,int32_t previous){
 printf("{\"case\":\"%s\",\"operation\":\"%s\",\"status\":\"%08x\"",c,op,s);
 if(has_previous)printf(",\"previous\":%d",previous);puts("}");
}
#include "../fixtures/nt_namespace_fixture.h"
int main(int argc,char **argv){
 check(argc==2,"reference target path argument");
 int64_t zero=0;
 for(unsigned kind=0;kind<2;kind++)for(unsigned initial=0;initial<2;initial++){
  char label[40],op[40];snprintf(label,sizeof(label),"type%u-initial%u",kind,initial);
  uint64_t h=0;uint32_t s=obj_event_create(&table,0x100003,kind,initial,&h);
  record(label,"create",s,0,0);check(s==0,"create reference event");
  for(unsigned phase=0;phase<2;phase++){
   if(phase)for(unsigned repeat=0;repeat<2;repeat++){
    int32_t previous=-999;snprintf(op,sizeof(op),"set%u",repeat);
    s=obj_event_set(&table,h,&previous);record(label,op,s,1,previous);
   }
   for(unsigned repeat=0;repeat<2;repeat++){
    snprintf(op,sizeof(op),"wait_%s%u",phase?"after_set":"initial",repeat);
    record(label,op,obj_event_wait(&table,h,0,&zero),0,0);
   }
  }
  record(label,"close",obj_close(&table,h),0,0);
 }
 record("null","wait",obj_event_wait(&table,0,0,&zero),0,0);
 record("null","set",obj_event_set(&table,0,0),0,0);
 record("null","close",obj_close(&table,0),0,0);
 /* Lifetime/access/exhaustion invariants beyond the native reference dataset. */
 uint64_t h=0,old=0,handles[OBJ_SLOTS];int32_t previous=123;
 check(!obj_event_create(&table,OBJ_SYNCHRONIZE,0,0,&h),"read-only event");
 check(obj_event_set(&table,h,&previous)==OBJ_ACCESS_DENIED&&previous==123,"denied set leaves output/state");
 check(obj_event_wait(&table,h,0,&zero)==OBJ_TIMEOUT,"denied set does not signal");
 old=h;check(!obj_close(&table,h),"close old");
 check(!obj_event_create(&table,OBJ_EVENT_MODIFY,1,1,&h)&&h!=old,"generation changes");
 check(obj_event_wait(&table,h,0,&zero)==OBJ_ACCESS_DENIED,"wait requires synchronize");
 check(obj_close(&table,old)==OBJ_INVALID_HANDLE,"stale close cannot close replacement");
 check(!obj_event_set(&table,h,&previous)&&previous==1,"replacement survives stale close");
 obj_reset(&table);check(obj_close(&table,h)==OBJ_INVALID_HANDLE,"reset invalidates handles");
 for(unsigned i=0;i<OBJ_SLOTS;i++)check(!obj_event_create(&table,OBJ_EVENT_ALL,0,0,&handles[i]),"fill table");
 h=0x1234;check(obj_event_create(&table,OBJ_EVENT_ALL,0,0,&h)==OBJ_NO_MEMORY&&h==0x1234,"exhaustion preserves output");
 check(obj_event_wait(&table,handles[0],0,0)==OBJ_NOT_SUPPORTED,"blocking wait explicit unsupported");
 check(obj_close(&table,handles[0]|1)==OBJ_INVALID_HANDLE,"malformed handle rejected");
 for(unsigned i=0;i<OBJ_SLOTS;i++)check(!obj_close(&table,handles[i]),"cleanup table");
 table.slots[0].generation=UINT32_MAX;
 check(!obj_event_create(&table,OBJ_EVENT_ALL,0,0,&h)&&table.slots[0].retired,"generation exhaustion retires slot");
 check(!obj_close(&table,h),"final cleanup");
 fprintf(stderr,"Object lifetime/access/exhaustion invariants passed\n");namespace_fixture(argv[1]);return 0;
}
