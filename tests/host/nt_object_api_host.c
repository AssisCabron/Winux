#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint8_t u8;typedef uint16_t u16;typedef uint32_t u32;typedef uint64_t u64;
#include "../../src/nt/objects/nt_objects.h"
static obj_table nt_objects;
static union {uint64_t align;unsigned char bytes[4096];} arena;
static int nt_obj_readable(const void *p,u64 n){uintptr_t a=(uintptr_t)p,b=(uintptr_t)arena.bytes;return a>=b&&a-b<sizeof(arena)&&n<=sizeof(arena)-(a-b);}
static int nt_obj_writable(const void *p,u64 n){return nt_obj_readable(p,n);}
static int same(const char *a,const char *b){return !strcmp(a,b);}
#include "../../src/nt/objects/nt_object_api.h"
static void check(int ok,const char *s){if(!ok){fprintf(stderr,"FAIL: %s\n",s);exit(1);}}
int main(void){
 void **out=(void**)(arena.bytes+0);int32_t *previous=(int32_t*)(arena.bytes+16);
 int64_t *timeout=(int64_t*)(arena.bytes+24);*timeout=0;
 nt_obj_attributes *oa=(nt_obj_attributes*)(arena.bytes+64);
 nt_obj_string *name=(nt_obj_string*)(arena.bytes+128),*target=(nt_obj_string*)(arena.bytes+160);
 u16 *text=(u16*)(arena.bytes+256);u32 *returned=(u32*)(arena.bytes+192);
 *out=(void*)0x1234;
 check(nt_CreateEvent((void**)1,0x100003,0,1,0)==0xc0000005U,"invalid output");
 check(!nt_CreateEvent(out,0x100003,0,1,0),"create adapter");void *event=*out;
 check(nt_SetEvent(event,(int32_t*)1)==0xc0000005U,"invalid previous pointer");
 check(nt_WaitForSingleObject(event,0,timeout)==OBJ_TIMEOUT,"bad set did not signal");
 check(!nt_SetEvent(event,previous)&&*previous==0,"signal adapter");
 check(nt_WaitForSingleObject(event,0,(int64_t*)1)==0xc0000005U,"invalid timeout pointer");
 check(!nt_WaitForSingleObject(event,0,timeout),"bad wait did not consume");
 check(nt_WaitForSingleObject(event,0,timeout)==OBJ_TIMEOUT,"synchronization event consumed");
 check(!nt_Close(event)&&nt_Close(event)==OBJ_INVALID_HANDLE,"adapter double close");
 *out=(void*)0x1234;oa->length=48;oa->name=name;
 check(nt_CreateEvent(out,0x100003,oa,1,0)==OBJ_NOT_SUPPORTED&&*out==(void*)0x1234,"named creation unsupported");
 const u16 path[]={'C',':','\\','W','i','n','d','o','w','s',0};
 check(!obj_configure_dll_path(&nt_objects,path,20),"fixture namespace config");
 const char *directory="\\KnownDlls";unsigned i=0;for(;directory[i];i++)text[i]=(u8)directory[i];
 name->length=name->maximum=(u16)(i*2);name->buffer=text;oa->attributes=0x40;
 check(!nt_OpenDirectoryObject(out,3,oa),"directory adapter");void *root=*out;
 oa->root=root;const char *link="KnownDllPath";for(i=0;link[i];i++)text[i]=(u8)link[i];name->length=name->maximum=(u16)(i*2);
 check(!nt_OpenSymbolicLinkObject(out,1,oa),"link adapter");void *h=*out;
 target->buffer=(u16*)(arena.bytes+512);target->maximum=22;
 check(nt_QuerySymbolicLinkObject(h,target,(u32*)1)==0xc0000005U,"invalid returned size pointer");
 check(!nt_QuerySymbolicLinkObject(h,target,returned)&&target->length==20&&*returned==22,"query adapter");
 target->buffer=(u16*)(arena.bytes+4094);target->maximum=22;
 check(nt_QuerySymbolicLinkObject(h,target,returned)==0xc0000005U,"cross-boundary output buffer");
 name->length=3;*out=(void*)0x1234;
 check(nt_OpenSymbolicLinkObject(out,1,oa)==OBJ_INVALID_PARAMETER&&*out==(void*)0x1234,"odd input length");
 check(!nt_Close(h)&&!nt_Close(root),"namespace cleanup");
 obj_reset(&nt_objects);
 const char *exports[]={"NtCreateEvent","NtSetEvent","NtWaitForSingleObject","NtClose","NtOpenDirectoryObject","NtOpenSymbolicLinkObject","NtQuerySymbolicLinkObject"};
 for(i=0;i<7;i++)check(nt_object_export(exports[i])!=0,"all seven direct bindings");
 check(!nt_object_export("NtMissing"),"unknown export");
 puts("NT object adapters: guest-range, no-side-effect, event and namespace checks passed");return 0;
}
