/* Execute only the retained NLS setup functions, not process startup.
 * Files are immutable RAMFS data pinned by prepare_windows.py. */
static int nt_nls_cp(const ram_file *f,u16 code,u64 *offsets){
 if(!f||f->size<28||(f->size&1)||rd16(f->data+2)!=code||rd16(f->data+4)!=1)return 0;
 u64 header=rd16(f->data),mb=(header+1)*2;
 if(!range(header*2,2,f->size)||!range(mb,514,f->size))return 0;
 u64 ranges=mb+514+(rd16(f->data+mb+512)?512:0);
 if(!range(ranges,2,f->size)||rd16(f->data+ranges))return 0; /* SBCS profile only. */
 u64 wide=((u16)(rd16(f->data+header*2)+header)+1ULL)*2;
 if(!range(wide,65536,f->size))return 0;
 offsets[0]=mb;offsets[1]=wide;offsets[2]=ranges;offsets[3]=0;return 1;
}
static int nt_nls_setup(pe_module *m){
 const ram_file *a=find_file("c_1252.nls"),*o=find_file("c_850.nls"),*c=find_file("l_intl.nls");
 u64 ao[4],oo[4];
 if(!m||!nt_nls_cp(a,1252,ao)||!nt_nls_cp(o,850,oo)||!c||c->size<6||(c->size&1))return 0;
 u64 scratch[4];ram_file short_table=*a;short_table.size=ao[1]+65534;
 if(nt_nls_cp(&short_table,1252,scratch))return 0;
 u8 bad_header[32]={255,255,0xe4,4,1};ram_file malformed={"bad.nls",bad_header,sizeof(bad_header)};
 if(nt_nls_cp(&malformed,1252,scratch)||nt_nls_cp(a,850,scratch))return 0;
 u64 lower=((u16)(rd16(c->data+2)+1)+1ULL)*2;
 if(!range(lower,2,c->size))return 0;
 if(m->state!=2||m->size<0x16db48)return 0;
 u64 init=module_export(m,"RtlInitNlsTables",0,0),reset=module_export(m,"RtlResetRtlTranslations",0,0);
 if(init!=(u64)m->base+0xe5430||reset!=(u64)m->base+0xe54b0)return 0;
 typedef void(*init_t)(const void*,const void*,const void*,void*);
 typedef void(*reset_t)(void*);
 u8 storage[0xb0] __attribute__((aligned(16)));u8 *info=storage+16;
 for(unsigned i=0;i<sizeof(storage);i++)storage[i]=0xa5;
 ((init_t)init)(a->data,o->data,c->data,info);
 int ok=nt_info_unchanged(storage,0,16)&&nt_info_unchanged(storage,0xa0,0xb0);
 const ram_file *files[]={o,a};u64 *offsets[]={oo,ao};
 for(unsigned j=0;j<2;j++){
  const u8 *p=info+j*0x40;
  if(rd16(p)!=(j?1252:850)||rd16(p+2)!=1||rd16(p+12))ok=0;
  const unsigned fields[]={0x20,0x28,0x30,0x38};
  for(unsigned k=0;k<4;k++)if(rd64(p+fields[k])!=(offsets[j][k]?(u64)files[j]->data+offsets[j][k]:0))ok=0;
 }
 if(rd64(info+0x80)!=(u64)c->data+4||rd64(info+0x88)!=(u64)c->data+lower)ok=0;
 if(!ok)return 0;
 ((reset_t)reset)(info);
 /* Verify publication into this mapped NTDLL, whose source hash is pinned. */
 if(rd64(m->base+0x16d708)!=(u64)a->data+ao[0]||rd64(m->base+0x16d700)!=(u64)a->data+ao[1]||
    rd64(m->base+0x16d710)!=(u64)o->data+oo[0]||rd64(m->base+0x16d720)!=(u64)o->data+oo[1]||
    rd64(m->base+0x16d730)!=(u64)c->data+4||rd64(m->base+0x16d6f8)!=(u64)c->data+lower)return 0;
 /* Discard temporary descriptors; the published pointers must remain valid. */
 for(unsigned i=0;i<sizeof(storage);i++)storage[i]=0xa5;
 if(*(const u16*)(rd64(m->base+0x16d708)+2*'A')!='A'||
    *(const u16*)(rd64(m->base+0x16d710)+2*'A')!='A')return 0;
 ((init_t)init)(0,0,0,info);
 for(unsigned j=0;j<2;j++)if(rd16(info+j*0x40)!=65001||rd16(info+j*0x40+2)!=4)return 0;
 if(rd64(info+0x80)||rd64(info+0x88)||!nt_info_unchanged(storage,0,16)||!nt_info_unchanged(storage,0xa0,0xb0))return 0;
 print("NT_NLS_OK: original Init/Reset, ANSI 1252, OEM 850, persistent tables, null UTF-8\n");return 1;
}

static int nt_nls_test(void){
 const ram_file *cmd=find_file("cmd.exe");if(!cmd)return 0;pe_load(cmd,0);
 return nt_nls_setup(module_named("ntdll.dll"));
}
