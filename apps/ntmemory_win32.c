/* Original freestanding x64 NT memory contract executable; no CRT. */
typedef unsigned int U32;typedef unsigned long long U64;typedef void *P;
#define API __declspec(dllimport)
API U32 NtAllocateVirtualMemory(P,P*,U64,U64*,U32,U32);
API U32 NtFreeVirtualMemory(P,P*,U64*,U32);
API U32 NtProtectVirtualMemory(P,P*,U64*,U32,U32*);
API U32 NtQueryVirtualMemory(P,P,U32,P,U64,U64*);
API P GetStdHandle(U32);API int WriteFile(P,const void*,U32,U32*,P);
typedef struct {U64 base,allocation;U32 allocation_protect;unsigned short partition,pad;U64 size;U32 state,protect,type,end_pad;} INFO;
static void text(const char *s){U32 n=0,w;while(s[n])n++;WriteFile(GetStdHandle((U32)-11),s,n,&w,0);}
static void number(U64 n){char b[24];U32 i=23,w;b[i]=0;do{b[--i]='0'+n%10;n/=10;}while(n);WriteFile(GetStdHandle((U32)-11),b+i,23-i,&w,0);}
static void record(const char *name,U32 status,U64 a,U64 b,U64 c){text("NT_MEMORY ");text(name);text(" ");number(status);text(",");number(a);text(",");number(b);text(",");number(c);text("\n");}
int mainCRTStartup(void){
 P process=(P)~0ULL,base=0,address;U64 n=12289,returned=0;U32 s,old;INFO q;
 s=NtAllocateVirtualMemory(process,&base,0,&n,0x2000,4);record("reserve",s,0,n,0);if(s||!base||n!=16384)return 10;
 int result=0;
 address=(P)((U64)base+4095);n=2;s=NtAllocateVirtualMemory(process,&address,0,&n,0x1000,4);
 record("commit",s,(U64)address-(U64)base,n,0);if(s){result=11;goto cleanup;}
 volatile unsigned char *p=base;if(p[0]||p[4096]){result=12;goto cleanup;}p[0]=93;p[4096]=71;
 address=base;n=8192;s=NtAllocateVirtualMemory(process,&address,0,&n,0x1000,2);
 record("recommit",s,(U64)address-(U64)base,n,0);if(s||p[0]!=93||p[4096]!=71){result=13;goto cleanup;}
 s=NtQueryVirtualMemory(process,base,0,&q,sizeof(q),&returned);
 record("query",s,q.size,returned,q.protect);if(s){result=14;goto cleanup;}
 address=(P)((U64)base+4097);n=1;old=0;s=NtProtectVirtualMemory(process,&address,&n,2,&old);
 record("protect",s,(U64)address-(U64)base,n,old);if(s){result=15;goto cleanup;}
 address=base;n=12288;old=0x1234;s=NtProtectVirtualMemory(process,&address,&n,4,&old);
 record("protect-reserved",s,(U64)address-(U64)base,n,old);if(!s){result=16;goto cleanup;}
 address=(P)((U64)base+1);n=1;s=NtFreeVirtualMemory(process,&address,&n,0x4000);
 record("decommit",s,(U64)address-(U64)base,n,0);if(s){result=17;goto cleanup;}
 address=base;n=1;s=NtAllocateVirtualMemory(process,&address,0,&n,0x1000,4);
 record("recommit-zero",s,(U64)address-(U64)base,n,s?0:p[0]);if(s||p[0]||p[4096]!=71){result=18;goto cleanup;}
 returned=0x1234;s=NtQueryVirtualMemory(process,base,0,&q,47,&returned);
 record("query-short",s,returned,0,0);if(!s){result=19;goto cleanup;}
 address=base;n=4096;s=NtFreeVirtualMemory(process,&address,&n,0);
 record("free-invalid-type",s,(U64)address-(U64)base,n,0);if(!s)return 20;
cleanup:
 address=base;n=0;s=NtFreeVirtualMemory(process,&address,&n,0x8000);
 record("release",s,(U64)address-(U64)base,n,0);if(s)return 21;
 if(!result)text("NT_MEMORY_OK\n");return result;
}
