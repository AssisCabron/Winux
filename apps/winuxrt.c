#pragma comment(linker, "/export:WinuxAdd,@7")
#pragma comment(linker, "/export:WinuxValue,@11,DATA")
#pragma comment(linker, "/export:WinuxAttached,@13,DATA")
#pragma comment(linker, "/export:OrdinalOnly,@17,NONAME")
__declspec(dllimport) void *GetStdHandle(unsigned);
__declspec(dllimport) int WriteFile(void*,const void*,unsigned,unsigned*,void*);
/* Original DLL, explicit entry point and exported writable data. */
unsigned _tls_index;
__attribute__((section(".tls$AAA"))) char dll_tls_start;
__thread __attribute__((section(".tls$BBB"))) volatile int dll_tls_value=17;
__thread __attribute__((section(".tls$BBB"))) volatile int dll_tls_zero;
__attribute__((section(".tls$ZZZ"))) char dll_tls_end;
static int dll_tls_ready;
static void dll_tls_callback(void*,unsigned,void*);
static void (*const dll_callbacks[])(void*,unsigned,void*)={dll_tls_callback,0};
__attribute__((used)) const struct {const void *start,*end;unsigned *index;const void *callbacks;unsigned zero,flags;}
_tls_used={&dll_tls_start,&dll_tls_end,&_tls_index,dll_callbacks,0,0x300000};
int WinuxValue=7;
int WinuxAttached;
int WinuxAdd(int a,int b){return WinuxAttached?a+b+WinuxValue:-1;}
int OrdinalOnly(int n){return WinuxAttached?n+WinuxValue:-3;}
int DllMain(void *module,unsigned reason,void *reserved){
 if(reason==1){if(!dll_tls_ready||dll_tls_value!=84||dll_tls_zero!=0)return 0;WinuxAttached=1;WinuxValue=11;}else if(reason==0){
  WinuxAttached=0;const char ok[]="DLL_EXIT_NOTIFY_OK\r\n",bad[]="DLL_EXIT_NOTIFY_BAD\r\n";unsigned n;
  WriteFile(GetStdHandle((unsigned)-11),reserved?ok:bad,reserved?sizeof(ok)-1:sizeof(bad)-1,&n,0);
 }
 return 1;
}

static void dll_tls_callback(void *module,unsigned reason,void *reserved){
 if(reason==1&&dll_tls_value==17&&dll_tls_zero==0){dll_tls_value=84;dll_tls_ready=1;}
 if(reason==0&&WinuxAttached==1&&dll_tls_value==84){
  const char message[]="DLL_STATIC_TLS_DETACH_OK\r\n";unsigned n;
  WriteFile(GetStdHandle((unsigned)-11),message,sizeof(message)-1,&n,0);
 }
}
