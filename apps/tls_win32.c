/* Original TLS directory and compiler-generated x64 TLS accesses, no CRT. */
typedef unsigned int u32;
typedef unsigned long long u64;
__declspec(dllimport) void *GetStdHandle(u32);
__declspec(dllimport) int WriteFile(void*,const void*,u32,u32*,void*);
__declspec(dllimport) __declspec(noreturn) void ExitProcess(u32);
u32 _tls_index;
__attribute__((section(".tls$AAA"))) char tls_start;
__thread __attribute__((section(".tls$BBB"))) volatile int tls_value=41;
__thread __attribute__((section(".tls$BBB"))) volatile int tls_zero;
__attribute__((section(".tls$ZZZ"))) char tls_end;
static int observed;
static void tls_callback(void *module,u32 reason,void *reserved){
 if(reason==1){if(tls_value==41&&tls_zero==0){tls_value=73;tls_zero=9;observed=1;}}
 if(reason==0){const char message[]="STATIC_TLS_DETACH_OK\r\n";u32 n;
  if(observed==2&&tls_value==99&&tls_zero==9)WriteFile(GetStdHandle((u32)-11),message,sizeof(message)-1,&n,0);
 }
}
static void (*const callbacks[])(void*,u32,void*)={tls_callback,0};
__attribute__((used)) const struct {
 const void *start,*end;u32 *index;const void *callbacks;u32 zero,flags;
} _tls_used={&tls_start,&tls_end,&_tls_index,callbacks,0,0x300000};
int mainCRTStartup(void){
 if(observed!=1||tls_value!=73||tls_zero!=9)return 31;
 tls_value=99;observed=2;
 const char message[]="STATIC_TLS_OK: template, zero, callback before entry, compiler GS access\r\n";u32 n;
 if(!WriteFile(GetStdHandle((u32)-11),message,sizeof(message)-1,&n,0))return 32;
 ExitProcess(0);
}
