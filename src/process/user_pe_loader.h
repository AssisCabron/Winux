/* USER-PE64: bounded PE32+ external-file loading into a managed CPL3
 * process. No imports, relocations, TLS, DLLs, CLR or unrestricted EXEs.
 * Shared Process Manager, VFS, FAT16 and native INT80; never pe_load(CPL0).
 */
#ifndef WINUX_USER_PE_LOADER_H
#define WINUX_USER_PE_LOADER_H
#include "../../build/user-pe-preview/user-pe-allowlist.h"
#define WX_PE_IMAGE_BASE (WX_USER_CODE-4096ULL)
static u16 wx_pe16(const u8 *p){return (u16)p[0]|((u16)p[1]<<8);}
static u32 wx_pe32(const u8 *p){
    return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24);
}
static u64 wx_pe64(const u8 *p){
    return (u64)wx_pe32(p)|((u64)wx_pe32(p+4)<<32);
}
static int wx_pe_bounds(u32 off,u32 count,u32 total){
    return off<=total && count<=total-off;
}
static u64 wx_pe_fnv64(const u8 *p,u32 n){
    u64 hash=14695981039346656037ULL;
    for(u32 i=0;i<n;i++)hash=(hash^p[i])*1099511628211ULL;
    return hash;
}
typedef struct {
    u32 raw_offset,raw_size,virtual_size,entry_rva;
} wx_pe_user_layout;
static int wx_pe_parse(const u8 *b,u32 size,wx_pe_user_layout *v){
    if(!b||!v||size!=1024||wx_pe16(b)!=0x5a4d)return 0;
    u32 pe=wx_pe32(b+0x3c);
    if(!wx_pe_bounds(pe,24+240+40,size) ||
       wx_pe32(b+pe)!=0x4550)return 0;
    const u8 *coff=b+pe+4,*o=b+pe+24,*s=o+240;
    if(wx_pe16(coff)!=0x8664 || wx_pe16(coff+2)!=1 ||
       wx_pe16(coff+16)!=240 || !(wx_pe16(coff+18)&2) ||
       wx_pe16(o)!=0x20b || wx_pe32(o+16)!=0x1000 ||
       wx_pe32(o+20)!=0x1000 || wx_pe64(o+24)!=WX_PE_IMAGE_BASE ||
       wx_pe32(o+32)!=4096 || wx_pe32(o+36)!=512 ||
       wx_pe32(o+56)!=8192 || wx_pe32(o+60)!=512 ||
       wx_pe16(o+68)!=3 || wx_pe32(o+108)!=16)return 0;
    /* No directories: imports, relocations, TLS, exceptions, CLR, etc. */
    for(u32 i=0;i<16;i++)
        if(wx_pe32(o+112+8*i)||wx_pe32(o+116+8*i))return 0;
    static const u8 section_name[]={'.','t','e','x','t'};
    for(u32 i=0;i<5;i++)if(s[i]!=section_name[i])return 0;
    for(u32 i=5;i<8;i++)if(s[i])return 0;
    u32 virt=wx_pe32(s+8),rva=wx_pe32(s+12);
    u32 raw=wx_pe32(s+16),offset=wx_pe32(s+20);
    if(!virt||virt>512||rva!=4096||raw!=512||offset!=512||
       wx_pe32(s+36)!=0x60000020U||
       !wx_pe_bounds(offset,raw,size))return 0;
    v->raw_offset=offset;v->raw_size=raw;
    v->virtual_size=virt;v->entry_rva=wx_pe32(o+16);
    return 1;
}
/* Fixture content allowlisted by host-derived whole-file hash. The path
 * alone never authorizes arbitrary executable bytes, even in test mode. */
static u64 wx_pe_allowed(const char *path){
    if(same(path,"APPS/USER64.EXE"))return WX_PE_HASH_USER;
    if(same(path,"APPS/WRITE64.EXE"))return WX_PE_HASH_WRITE;
    if(same(path,"APPS/NX64.EXE"))return WX_PE_HASH_NX;
    if(same(path,"APPS/BAD64.EXE"))return WX_PE_HASH_BAD;
    return 0;
}
static int wx_pe_map(wx_managed_process *p,const u8 *buffer,u32 size,
                     const wx_pe_user_layout *v){
    if(!p||!p->space.pt||!p->space.code||!v||!buffer||size!=1024)return 0;
    /* Preferred PE image base lies one page BEFORE a 2MiB boundary.
     * It therefore needs a distinct PT in the prior PDE. Writing into
     * the code PT at index 511 would silently map the WRONG virtual page. */
    const u32 header_pd=(u32)((WX_PE_IMAGE_BASE>>21)&511U);
    const u32 code_pd=(u32)((WX_USER_CODE>>21)&511U);
    const u32 header_slot=(u32)((WX_PE_IMAGE_BASE>>12)&511U);
    if(header_pd==code_pd || p->space.pd[header_pd] ||
       (WX_PE_IMAGE_BASE>>30)!=(WX_USER_CODE>>30))return 0;
    u64 *header_pt=page_alloc();
    if(!header_pt)return 0;
    p->space.image_header_pt=header_pt;
    void *header=page_alloc();
    if(!header)return 0;
    p->space.image_header=header;
    pg_zero(header_pt,4096);
    pg_zero(header,4096);
    pg_zero(p->space.code,4096);
    for(u32 i=0;i<512;i++)((u8*)header)[i]=buffer[i];
    for(u32 i=0;i<v->raw_size;i++)((u8*)p->space.code)[i]=buffer[v->raw_offset+i];
    /* Distinct supervisor-owned frames: PE headers R/NX; code RX; stack RW/NX. */
    header_pt[header_slot]=(u64)header|PG_PRESENT|PG_USER|PG_NX;
    p->space.pd[header_pd]=(u64)header_pt|PG_PRESENT|PG_WRITE|PG_USER;
    const u32 code_slot=(u32)((WX_USER_CODE>>12)&511U);
    const u32 stack_slot=(u32)((WX_USER_STACK>>12)&511U);
    return (p->space.pt[code_slot]&(PG_PRESENT|PG_USER|PG_WRITE|PG_NX))==
           (PG_PRESENT|PG_USER) &&
           (p->space.pt[stack_slot]&(PG_PRESENT|PG_USER|PG_WRITE|PG_NX))==
           (PG_PRESENT|PG_USER|PG_WRITE|PG_NX);
}
static int wx_user_pe_exec(const char *path){
    if(!path||!wx_pe_allowed(path)||wx_active){
        print("USER_PE_REJECTED: unknown/untrusted path\n");return 0;
    }
    if(!wx_root_volume.type){
        if(!wx_virtio_blk_detect()||wx_block_count!=1||
           !wx_fat_mount_volume(&wx_block_devices[0])){
            print("USER_PE_STORAGE_FAILED\n");return 0;
        }
    }
    u32 bytes=0;
    if(!wx_vfs_read_file(path,wx_vfs_scratch,sizeof(wx_vfs_scratch),&bytes)){
        print("USER_PE_NOT_FOUND: VFS FAT16 read failed\n");return 0;
    }
    if(wx_pe_fnv64(wx_vfs_scratch,bytes)!=wx_pe_allowed(path)){
        print("USER_PE_REJECTED: file integrity mismatch\n");return 0;
    }
    wx_pe_user_layout pe={0};
    if(!wx_pe_parse(wx_vfs_scratch,bytes,&pe)){
        print("USER_PE_REJECTED: malformed or unsupported PE32+\n");return 0;
    }
    if(!wx_prepare_tss())return 0;
    const u32 free_before=wx_proc_free_pages(),tables_before=pg_used;
    wx_managed_process *p=wx_proc_spawn(0);
    if(!p){print("USER_PE_SPAWN_FAILED\n");return 0;}
    print("PROCESS_CREATED pid=");number(p->pid);print("\n");
    int mapped=wx_pe_map(p,wx_vfs_scratch,bytes,&pe);
    u32 exitcode=0;u64 vector=0,error=0;
    int ran=0;
    if(mapped)
        ran=wx_proc_run_entry(p,WX_PE_IMAGE_BASE+pe.entry_rva,0,
                              &exitcode,&vector,&error);
    else p->state=WX_PROC_FAILED;
    if(!ran && p->state==WX_PROC_READY)p->state=WX_PROC_FAILED;
    print("PROCESS_EXIT code=");number(exitcode);
    print(" fault=");number(vector);
    print(" error=");print_hex(error);print("\n");
    int expected_fault=0;
    if(same(path,"APPS/WRITE64.EXE"))
        expected_fault=vector==14 && (error&0x7ULL)==0x7ULL;
    if(same(path,"APPS/NX64.EXE"))
        expected_fault=vector==14 && (error&0x15ULL)==0x15ULL;
    if(expected_fault)print("USER_PE_FAULT_CONTAINED_OK\n");
    int clean=wx_proc_reap(p) &&
              wx_proc_free_pages()==free_before && pg_used==tables_before;
    print(clean?"PROCESS_REAP_OK: frames and handles restored\n":
                "PROCESS_REAP_FAILED\n");
    int ok=clean && (ran || expected_fault);
    if(ok && ran)print("USER_PE_LIFECYCLE_OK: FAT16 PE32+ CPL3 execution\n");
    if(ok && expected_fault)print("USER_PE_SECURITY_OK: CPL3 fault isolated\n");
    if(!ok)print("USER_PE_RUN_FAILED\n");
    return ok;
}
#endif
