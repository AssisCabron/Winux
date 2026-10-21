/* Original freestanding Windows x64 contract test, no CRT. */
typedef unsigned int DWORD;
typedef unsigned short WCHAR;
typedef unsigned long long SIZE_T;
typedef void *HANDLE;
#define API __declspec(dllimport)
API HANDLE GetStdHandle(DWORD);
API int WriteFile(HANDLE,const void*,DWORD,DWORD*,void*);
API int WriteConsoleW(HANDLE,const WCHAR*,DWORD,DWORD*,void*);
API int GetConsoleMode(HANDLE,DWORD*);
API int SetConsoleMode(HANDLE,DWORD);
API DWORD GetConsoleOutputCP(void);
API int SetConsoleOutputCP(DWORD);
API DWORD GetLastError(void);
API void SetLastError(DWORD);
API DWORD GetCurrentProcessId(void);
API DWORD GetCurrentThreadId(void);
API __declspec(noreturn) void ExitProcess(DWORD);
API WCHAR *GetCommandLineW(void);
API HANDLE GetProcessHeap(void);
API void *HeapAlloc(HANDLE,DWORD,SIZE_T);
API int HeapFree(HANDLE,DWORD,void*);
API void *HeapReAlloc(HANDLE,DWORD,void*,SIZE_T);
API SIZE_T HeapSize(HANDLE,DWORD,void*);
API DWORD TlsAlloc(void);
API int TlsFree(DWORD);
API int TlsSetValue(DWORD,void*);
API void *TlsGetValue(DWORD);
API int SetEnvironmentVariableW(const WCHAR*,const WCHAR*);
API DWORD GetEnvironmentVariableW(const WCHAR*,WCHAR*,DWORD);
API DWORD GetTickCount(void);
API SIZE_T GetTickCount64(void);
API int QueryPerformanceCounter(SIZE_T*);
API int QueryPerformanceFrequency(SIZE_T*);
static int emit(const char *s,DWORD n){DWORD written;return WriteFile(GetStdHandle((DWORD)-11),s,n,&written,0)&&written==n;}
#define REPORT(text) do{if(!emit(text,sizeof(text)-1))return 90;}while(0)
#define CHECK(test,code) do{if(!(test))return code;}while(0)
int mainCRTStartup(void){
 SIZE_T self,peb;__asm__ volatile("movq %%gs:0x30,%0":"=r"(self));
 CHECK(self&&*(SIZE_T*)(self+0x30)==self,10);
 SIZE_T here=(SIZE_T)&self;
 CHECK(here>=*(SIZE_T*)(self+0x10)&&here<*(SIZE_T*)(self+8),11);
 CHECK(*(SIZE_T*)(self+0x40)==GetCurrentProcessId()&&*(SIZE_T*)(self+0x48)==GetCurrentThreadId(),12);
 __asm__ volatile("movq %%gs:0x60,%0":"=r"(peb));CHECK(peb!=0,13);
 SetLastError(0x12345678);CHECK(GetLastError()==0x12345678&&*(DWORD*)(self+0x68)==0x12345678,14);
 CHECK(GetCommandLineW()&&GetCommandLineW()[0],15);
 REPORT("CONTEXT_OK: GS, stack, process, thread, last-error, command line\r\n");

 HANDLE heap=GetProcessHeap();CHECK(heap!=0,20);
 unsigned char *p=HeapAlloc(heap,8,37);CHECK(p&&((SIZE_T)p&15)==0&&HeapSize(heap,0,p)>=37,21);
 for(DWORD i=0;i<37;i++){CHECK(p[i]==0,22);p[i]=(unsigned char)(i+1);}
 CHECK(!HeapAlloc(heap,0,~(SIZE_T)0),63);
 CHECK(!HeapReAlloc(heap,0,p,~(SIZE_T)0)&&p[36]==37&&HeapSize(heap,0,p)>=37,64);
 unsigned char *q=HeapReAlloc(heap,8,p,513);CHECK(q&&HeapSize(heap,0,q)>=513,23);
 for(DWORD i=0;i<513;i++)CHECK(q[i]==(i<37?i+1:0),24);
 void *block=HeapAlloc(heap,0,127);CHECK(block,25);
 CHECK(HeapFree(heap,0,q)&&HeapFree(heap,0,block),26);
 /* Repeated mixed sizes expose fragmentation and missing coalescing. */
 void *blocks[24];for(DWORD round=0;round<8;round++){
  for(DWORD i=0;i<24;i++){blocks[i]=HeapAlloc(heap,8,64+i*71);CHECK(blocks[i],27);}
  for(DWORD i=0;i<24;i+=2)CHECK(HeapFree(heap,0,blocks[i]),28);
  for(DWORD i=1;i<24;i+=2)CHECK(HeapFree(heap,0,blocks[i]),29);
 }
 REPORT("HEAP_OK: alignment, zeroing, realloc, preservation, reuse\r\n");

 DWORD slot=TlsAlloc();CHECK(slot!=~0U,30);SetLastError(99);
 CHECK(TlsGetValue(slot)==0&&GetLastError()==0,31);
 CHECK(TlsSetValue(slot,(void*)0x1234)&&TlsGetValue(slot)==(void*)0x1234,32);
 CHECK(TlsFree(slot),33);
 DWORD reused=TlsAlloc();CHECK(reused!=~0U&&TlsGetValue(reused)==0,34);CHECK(TlsFree(reused),35);
 REPORT("TLS_OK: zero initial value, stored value, last-error, reuse\r\n");

 static const WCHAR name[]=L"WINUX_CONTRACT_VAR_04";
 static const WCHAR lower[]=L"winux_contract_var_04";
 static const WCHAR value[]=L"Alpha";
 WCHAR result[16];
 CHECK(!GetEnvironmentVariableW(L"WINUX_RESET_PROBE_04",result,16)&&GetLastError()==203,48);
 CHECK(SetEnvironmentVariableW(L"WINUX_RESET_PROBE_04",L"sticky"),49);
 CHECK(SetEnvironmentVariableW(name,value),40);
 CHECK(GetEnvironmentVariableW(lower,0,0)==6,41);
 CHECK(GetEnvironmentVariableW(name,result,2)==6,42);
 CHECK(GetEnvironmentVariableW(lower,result,16)==5&&result[0]=='A'&&result[4]=='a'&&!result[5],43);
 CHECK(SetEnvironmentVariableW(name,L""),44);
 SetLastError(0);CHECK(GetEnvironmentVariableW(name,result,16)==0&&result[0]==0&&GetLastError()==0,45);
 CHECK(SetEnvironmentVariableW(name,0),46);
 CHECK(GetEnvironmentVariableW(name,result,16)==0&&GetLastError()==203,47);
 REPORT("ENV_OK: case, size query, short buffer, empty value, deletion\r\n");

 DWORD mode,written=0;HANDLE output=GetStdHandle((DWORD)-11);
 CHECK(GetConsoleMode(output,&mode)&&SetConsoleMode(output,mode),50);
 CHECK(!GetConsoleMode((HANDLE)0,&mode)&&GetLastError()==6,51);
 CHECK(!WriteConsoleW((HANDLE)0,L"X",1,&written,0)&&GetLastError()==6,52);
 CHECK(WriteConsoleW(output,L"",0,&written,0)&&written==0,53);
 static const WCHAR unicode[]=L"UTF16_OK: a\u00e7\u00e3o, caf\u00e9, \u03a9, \U0001f680\r\n";
 CHECK(WriteConsoleW(output,unicode,sizeof(unicode)/2-1,&written,0)&&written==sizeof(unicode)/2-1,54);
 CHECK(SetConsoleOutputCP(65001)&&GetConsoleOutputCP()==65001,55);
 REPORT("UTF8_OK: a\xc3\xa7\xc3\xa3o, caf\xc3\xa9, \xce\xa9, \xf0\x9f\x9a\x80\r\n");
 SIZE_T freq,a,b;CHECK(QueryPerformanceFrequency(&freq)&&freq>0,60);
 CHECK(QueryPerformanceCounter(&a)&&QueryPerformanceCounter(&b)&&b>=a,61);
 DWORD tick=GetTickCount();SIZE_T tick64=GetTickCount64();CHECK((DWORD)(tick64-tick)<1000,62);
 REPORT("TIMER_OK: monotonic counter, frequency, 32/64 bit ticks\r\n");
 REPORT("RUNTIME_CONTRACTS_OK\r\n");
 /* ExitProcess bypasses our epilogue: launcher must restore FP control state. */
 DWORD mxcsr;WCHAR x87;
 __asm__ volatile("stmxcsr %0; fnstcw %1":"=m"(mxcsr),"=m"(x87));
 mxcsr=(mxcsr&~0x6000U)|0x6000U;x87=(WCHAR)((x87&~0xc00U)|0xc00U);
 __asm__ volatile("ldmxcsr %0; fldcw %1"::"m"(mxcsr),"m"(x87));
 ExitProcess(0);
}
