/* Same original binary+DLLs are tested on Windows and Winux. No CRT. */
typedef unsigned int DWORD;typedef unsigned short WCHAR;
#define API __declspec(dllimport)
API int WinuxValue;
API int WinuxAttached;
API int WinuxAdd(int,int);
API int BridgeAdd(int,int);
API int OrdinalOnly(int);
API void *GetModuleHandleW(const WCHAR*);
API void *GetProcAddress(void*,const char*);
API DWORD GetLastError(void);
API __declspec(noreturn) void ExitProcess(DWORD);
API void *GetStdHandle(DWORD);
API int WriteFile(void*,const void*,DWORD,DWORD*,void*);
int mainCRTStartup(void){
 if(WinuxAttached!=1||WinuxValue!=11||WinuxAdd(2,3)!=16||BridgeAdd(4,5)!=20)return 10;
 WinuxValue=20;if(WinuxAdd(2,3)!=25||BridgeAdd(4,5)!=29)return 11;
 void *module=GetModuleHandleW(L"WINUXRT.DLL");if(!module)return 12;
 int (*add)(int,int)=(int(*)(int,int))GetProcAddress(module,"WinuxAdd");
 if(!add||add(3,4)!=27||GetProcAddress(module,"WinuxValue")!=&WinuxValue)return 13;
 if(GetProcAddress(module,"does_not_exist")||GetLastError()!=127)return 14;
 if((void*)add!=GetProcAddress(module,(const char*)7)||OrdinalOnly(2)!=22)return 16;
 void *bridge=GetModuleHandleW(L"winuxbridge.dll");if(!bridge)return 17;
 if(GetProcAddress(bridge,"ForwardAdd")!=(void*)add||GetProcAddress(bridge,"ForwardData")!=&WinuxValue||GetProcAddress(bridge,"ForwardOrdinal")!=(void*)add)return 18;
 const char message[]="DLL_GRAPH_OK: init order, functions, data, ordinals, forwarders, dynamic exports\r\n";
 DWORD written;if(!WriteFile(GetStdHandle((DWORD)-11),message,sizeof(message)-1,&written,0)||written!=sizeof(message)-1)return 15;
#ifdef TEST_EXIT_PROCESS
 ExitProcess(0);
#else
 return 0;
#endif
}
