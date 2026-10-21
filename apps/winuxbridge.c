#pragma comment(linker, "/export:BridgeAdd")
#pragma comment(linker, "/export:ForwardAdd=winuxrt.WinuxAdd")
#pragma comment(linker, "/export:ForwardData=winuxrt.WinuxValue,DATA")
#pragma comment(linker, "/export:ForwardOrdinal=winuxrt.#7")
/* Original dependent DLL verifies dependency-before-importer DllMain ordering. */
__declspec(dllimport) int WinuxAdd(int,int);
static int ready;
int BridgeAdd(int a,int b){return ready?WinuxAdd(a,b):-2;}
int DllMain(void *module,unsigned reason,void *reserved){
 if(reason==1){ready=WinuxAdd(1,2)==14;return ready;}if(reason==0)ready=0;return 1;
}
