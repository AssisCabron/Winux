static unsigned wide(uint16_t *out,const char *s){unsigned n=0;for(;s[n];n++){check((unsigned char)s[n]<128&&n<259,"ASCII fixture path");out[n]=(unsigned char)s[n];}out[n]=0;return n*2;}
static void json_string(const char *s){putchar('"');for(;*s;s++){if(*s=='\\'||*s=='"')putchar('\\');putchar(*s);}putchar('"');}
static uint32_t fixture_open(const char *op,unsigned kind,uint64_t root,const char *name,unsigned access,uint64_t *out){
 uint16_t w[260];unsigned n=wide(w,name);uint32_t s=obj_namespace_open(&table,kind,root,w,n,1,access,out);
 printf("{\"namespace\":true,\"operation\":\"%s\",\"status\":\"%08x\",\"name\":",op,s);json_string(name);
 printf(",\"relative\":%s,\"access\":%u}\n",root?"true":"false",access);return s;
}
static void namespace_fixture(const char *target){
 obj_reset(&table);uint16_t w[260];unsigned bytes=wide(w,target);
 check(!obj_configure_dll_path(&table,w,bytes),"configure namespace");
 uint64_t dir=0,link=0,h=0;
 check(!fixture_open("NtOpenDirectoryObject",2,0,"\\KnownDlls",3,&dir),"open directory");
 check(!fixture_open("NtOpenSymbolicLinkObject",3,dir,"KnownDllPath",1,&link),"open link");
 const unsigned capacities[]={0,2,38,39,40,48,512};
 for(unsigned i=0;i<sizeof(capacities)/sizeof(capacities[0]);i++){
  uint16_t buffer[260],length=0;uint32_t required=0xffffffff;
  for(unsigned j=0;j<260;j++)buffer[j]=0xa5a5;
  uint32_t s=obj_link_query(&table,link,buffer,capacities[i],&length,&required);
  printf("{\"namespace\":true,\"operation\":\"query\",\"status\":\"%08x\",\"capacity\":%u,\"length\":%u,\"required\":%u,\"value\":",s,capacities[i],length,required);
  if(!s){char text[260];for(unsigned j=0;j<=length/2;j++)text[j]=(char)buffer[j];json_string(text);check(buffer[length/2+1]==0xa5a5,"query trailing canary");}
  else {printf("null");check(buffer[0]==0xa5a5,"short query preserves buffer");}puts("}");
 }
 check(fixture_open("NtOpenSymbolicLinkObject",3,dir,"Winux_Probe_Missing_Link_6c82791a",1,&h)==OBJ_NAME_NOT_FOUND,"missing link");
 printf("{\"namespace\":true,\"operation\":\"close\",\"status\":\"%08x\"}\n",obj_close(&table,link));
 printf("{\"namespace\":true,\"operation\":\"close\",\"status\":\"%08x\"}\n",obj_close(&table,dir));
 /* Separate invariants; not claimed as native-reference comparisons. */
 unsigned n=wide(w,"\\knowndlls");h=123;
 check(obj_namespace_open(&table,2,0,w,n,0,3,&h)==OBJ_NAME_NOT_FOUND&&h==123,"case-sensitive lookup");
 check(!obj_namespace_open(&table,2,0,w,n,1,3,&dir),"case-insensitive lookup");
 check(obj_event_set(&table,dir,0)==OBJ_TYPE_MISMATCH,"event rejects directory handle");
 n=wide(w,"KnownDllPath");
 check(obj_namespace_open(&table,2,dir,w,n,1,1,&h)==OBJ_TYPE_MISMATCH,"wrong requested type");
 check(!obj_namespace_open(&table,3,dir,w,n,1,1,&link),"reopen link");
 check(!obj_close(&table,dir),"close root while link stays open");
 uint16_t buffer[260],length=0;uint32_t required=0;
 check(!obj_link_query(&table,link,buffer,sizeof(buffer),&length,&required),"link survives root close");
 check(obj_namespace_open(&table,3,dir,w,n,1,1,&h)==OBJ_INVALID_HANDLE,"stale root rejected");
 check(!obj_close(&table,link),"close link");
 n=wide(w,"\\KnownDlls");check(!obj_namespace_open(&table,2,0,w,n,1,1,&dir),"root without traverse");
 n=wide(w,"KnownDllPath");check(obj_namespace_open(&table,3,dir,w,n,1,1,&h)==OBJ_ACCESS_DENIED,"traverse right enforced");
 obj_reset(&table);fprintf(stderr,"Namespace type/access/lifetime/buffer invariants passed\n");
}
