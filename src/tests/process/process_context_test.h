static int process_context_test(void){
 const ram_file *cmd=find_file("cmd.exe");if(!cmd)return 0;pe_load(cmd,0);
 pe_module *m=module_named("ntdll.dll");if(!m||m->state!=2)return 0;
 u64 normalizer=module_export(m,"RtlNormalizeProcessParams",0,0);
 if(normalizer!=(u64)m->base+0x892e0||rd64(app_peb+0x20)!=(u64)app_parameters||rd32(app_peb+0xb8)!=1)return 0;
 if(rd64(app_teb+0x60)!=(u64)app_peb||win_GetCommandLineW()!=(u16*)rd64(app_parameters+0x78))return 0;
 for(unsigned i=0;i<3;i++)if(rd64(app_parameters+0x20+i*8)!=(u64)win_GetStdHandle((u32)-10-i))return 0;
 static u8 prior[sizeof(app_parameters)];for(unsigned i=0;i<sizeof(prior);i++)prior[i]=app_parameters[i];
 if(((void*(*)(void*))normalizer)(app_parameters)!=app_parameters)return 0;
 for(unsigned i=0;i<sizeof(prior);i++)if(prior[i]!=app_parameters[i])return 0;
 u16 name[]={'Z','_','T','E','S','T',0},value[]={'o','k',0},readback[256];
 u64 env=rd64(app_parameters+0x80);
 if(env!=(u64)app_environment||!win_SetEnvironmentVariableW(name,value)||rd64(app_parameters+0x80)!=env)return 0;
 int found=0;unsigned units=(unsigned)(app_environment_bytes/2);
 for(unsigned i=0;i+9<units;i++){
  if(i&&app_environment[i-1])continue;unsigned j=0;while(j<6&&app_environment[i+j]==name[j])j++;
  if(j==6&&app_environment[i+6]=='='&&app_environment[i+7]=='o'&&app_environment[i+8]=='k'&&!app_environment[i+9])found=1;
 }
 if(!found||win_GetEnvironmentVariableW(name,readback,256)!=2||readback[0]!='o'||readback[1]!='k')return 0;
 u16 empty[]={0};if(!win_SetEnvironmentVariableW(name,empty)||win_GetEnvironmentVariableW(name,readback,256)||readback[0])return 0;
 if(!win_SetEnvironmentVariableW(name,0)||win_GetEnvironmentVariableW(name,readback,256)||last_error!=203)return 0;
 /* Remove defaults through the API, then exercise every bounded slot. */
 for(unsigned i=0;i<16;i++)if(app_env[i].used){u16 key[64];for(unsigned j=0;j<64;j++)key[j]=app_env[i].name[j];if(!win_SetEnvironmentVariableW(key,0))return 0;}
 if(app_environment[0]||app_environment[1])return 0;
 u16 key[64],large[256];for(unsigned i=0;i<63;i++)key[i]='a';key[63]=0;
 for(unsigned i=0;i<255;i++)large[i]='v';large[255]=0;
 for(unsigned i=0;i<16;i++){key[62]=(u16)('A'+i);if(!win_SetEnvironmentVariableW(key,large))return 0;}
 if(app_environment_bytes!=10248||rd64(app_parameters+0x3f0)!=app_environment_bytes||rd64(app_parameters+0x80)!=env)return 0;
 key[62]='Z';if(win_SetEnvironmentVariableW(key,large)||last_error!=8)return 0;
 key[62]='A';if(win_GetEnvironmentVariableW(key,readback,256)!=255||readback[254]!='v'||readback[255])return 0;
 if(!runtime_reset("cmd.exe")||win_GetEnvironmentVariableW(key,readback,256)||last_error!=203)return 0;
 if(rd64(app_peb+0x20)!=(u64)app_parameters||rd64(app_parameters+0x80)!=env)return 0;
 print("PROCESS_CONTEXT_OK: PEB parameters, original normalization, console handles, synchronized environment, bounds, reset\n");return 1;
}
