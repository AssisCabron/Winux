/* FAT16 read-only root directory backed by the live wx_block_device.
 * Explicit bounds, chain validation, no LFN/subdir/write/metadata changes.
 * Requires FAT16 cluster-count range; refuse FAT12/FAT32 instead of guessing.
 */
#ifndef WINUX_FAT16_RO_H
#define WINUX_FAT16_RO_H
typedef struct {
    wx_block_device *disk;
    u32 reserved,fat_size,first_root,root_sectors,first_data;
    u32 total_sectors,data_clusters,root_entries,spc;
} wx_fat16_mount;
static wx_fat16_mount wx_fat16_ctx;
static u8 wx_fat16_sector[512];
static u8 wx_vfs_scratch[WX_VFS_MAX_READ];
static u16 wx_fat_get16(const u8 *p){return (u16)p[0]|((u16)p[1]<<8);}
static u32 wx_fat_get32(const u8 *p){return (u32)wx_fat_get16(p)|((u32)wx_fat_get16(p+2)<<16);}
static int wx_fat_disk_read(wx_fat16_mount *f,u32 lba,u8 *dest){
    return f && f->disk && lba<f->total_sectors && wx_block_read(f->disk,lba,1,dest);
}
static int wx_fat_name(const char *path,u8 out[11]){
    if(!path || !*path)return 0;
    for(unsigned i=0;i<11;i++)out[i]=' ';
    unsigned name=0,ext=0,in_ext=0;
    for(const char *p=path;*p;p++){
        unsigned char c=(unsigned char)*p;
        if(c=='.' && !in_ext && name){in_ext=1;continue;}
        if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||
             (c>='0'&&c<='9')||c=='_'||c=='-'))return 0;
        if(c>='a'&&c<='z')c-=32;
        if(!in_ext){if(name==8)return 0;out[name++]=c;}
        else{if(ext==3)return 0;out[8+ext++]=c;}
    }
    return name>0 && (!in_ext || ext>0);
}
static int wx_fat_lookup(wx_vfs_volume *volume,const char *path,wx_vfs_entry *found){
    wx_fat16_mount *f=(wx_fat16_mount*)volume->backend;
    u8 query[11];
    if(!f||!found||!wx_fat_name(path,query))return 0;
    for(u32 n=0;n<f->root_entries;n++){
        if(!wx_fat_disk_read(f,f->first_root+n/16,wx_fat16_sector))return 0;
        const u8 *e=wx_fat16_sector+(n%16)*32;
        if(!e[0])return 0;
        if(e[0]==0xe5 || (e[11]&0x18) || (e[11]&0x0f)==0x0f)continue;
        int matched=1;
        for(unsigned i=0;i<11;i++)if(e[i]!=query[i])matched=0;
        if(!matched)continue;
        for(unsigned i=0;i<12;i++)found->name[i]=0;
        for(unsigned i=0;i<8 && e[i]!=' ';i++)found->name[i]=(char)e[i];
        unsigned off=0;
        while(off<8 && found->name[off])off++;
        if(e[8]!=' '){found->name[off++]='.';for(unsigned i=8;i<11 && e[i]!=' ';i++)found->name[off++]=(char)e[i];}
        found->name[off]=0;
        found->first_cluster=wx_fat_get16(e+26);
        found->size=wx_fat_get32(e+28);
        return found->size>0 && found->first_cluster>=2 &&
               found->first_cluster<f->data_clusters+2;
    }
    return 0;
}
static int wx_fat_next_cluster(wx_fat16_mount *f,u32 cluster,u32 *next){
    if(cluster<2 || cluster>=f->data_clusters+2)return 0;
    u32 offset=cluster*2;
    if(offset/512>=f->fat_size || !wx_fat_disk_read(f,f->reserved+offset/512,wx_fat16_sector))return 0;
    *next=wx_fat_get16(wx_fat16_sector+offset%512);
    return 1;
}
static int wx_fat_read(wx_vfs_volume *volume,const wx_vfs_entry *file,u8*out,u32 capacity){
    wx_fat16_mount *f=(wx_fat16_mount*)volume->backend;
    if(!f||!file||!out||file->size>capacity||file->size>WX_VFS_MAX_READ)return 0;
    u32 remain=file->size,cluster=file->first_cluster,pos=0;
    u32 cluster_limit=(file->size+f->spc*512-1)/(f->spc*512);
    if(!cluster_limit || cluster_limit>WX_VFS_MAX_READ/512)return 0;
    for(u32 count=0;count<cluster_limit;count++){
        if(cluster<2 || cluster>=f->data_clusters+2)return 0;
        u32 lba=f->first_data+(cluster-2)*f->spc;
        for(u32 sec=0;sec<f->spc && remain;sec++){
            if(!wx_fat_disk_read(f,lba+sec,wx_fat16_sector))return 0;
            u32 n=remain<512?remain:512;
            for(u32 i=0;i<n;i++)out[pos+i]=wx_fat16_sector[i];
            pos+=n;remain-=n;
        }
        if(!remain)return 1;
        u32 next=0;
        if(!wx_fat_next_cluster(f,cluster,&next)||
           next>=0xfff8 || next<2 || next>=f->data_clusters+2)return 0;
        cluster=next;
    }
    return remain==0;
}
static int wx_fat_list(wx_vfs_volume *volume){
    wx_fat16_mount *f=(wx_fat16_mount*)volume->backend;
    if(!f)return 0;
    print("VFS_LIST_BEGIN\n");
    unsigned listed=0;
    for(u32 n=0;n<f->root_entries;n++){
        if(!wx_fat_disk_read(f,f->first_root+n/16,wx_fat16_sector))return 0;
        const u8 *e=wx_fat16_sector+(n%16)*32;
        if(!e[0])break;
        if(e[0]==0xe5 || (e[11]&0x18) || (e[11]&0x0f)==0x0f)continue;
        wx_vfs_entry entry={0};
        char name[13]={0};
        unsigned off=0;
        for(unsigned i=0;i<8 && e[i]!=' ';i++)name[off++]=(char)e[i];
        if(e[8]!=' '){name[off++]='.';for(unsigned i=8;i<11 && e[i]!=' ';i++)name[off++]=(char)e[i];}
        if(!wx_fat_lookup(volume,name,&entry))return 0;
        print("VFS_FILE ");print(entry.name);print(" bytes=");number(entry.size);print("\n");
        listed++;
    }
    print("VFS_LIST_END\n");
    return listed>0;
}
static int wx_fat_mount_volume(wx_block_device *device){
    if(!device || !device->read || device->sector_bytes!=512)return 0;
    u8 b[512];
    if(!wx_block_read(device,0,1,b) || b[510]!=0x55 || b[511]!=0xaa)return 0;
    u32 bps=wx_fat_get16(b+11),spc=b[13],reserved=wx_fat_get16(b+14);
    u32 nfats=b[16],entries=wx_fat_get16(b+17),fat_size=wx_fat_get16(b+22);
    u32 total=wx_fat_get16(b+19);
    if(!total)total=wx_fat_get32(b+32);
    if(bps!=512 || spc!=1 || !reserved || !fat_size || nfats!=2 ||
       entries!=512 || total>device->capacity_sectors || total<4096)return 0;
    u32 root_sectors=(entries*32+511)/512;
    u32 first_root=reserved+nfats*fat_size,first_data=first_root+root_sectors;
    if(first_data>=total)return 0;
    u32 clusters=(total-first_data)/spc;
    if(clusters<4085 || clusters>=65525 || fat_size*512<(clusters+2)*2)return 0;
    wx_fat16_ctx=(wx_fat16_mount){device,reserved,fat_size,first_root,root_sectors,
                                  first_data,total,clusters,entries,spc};
    wx_vfs_volume mount={"FAT16-ro",&wx_fat16_ctx,device,wx_fat_lookup,wx_fat_read,wx_fat_list};
    return wx_vfs_mount(&mount);
}
static int wx_vfs_fat_test(void){
    if(!wx_virtio_blk_detect() || wx_block_count!=1 ||
       !wx_fat_mount_volume(&wx_block_devices[0]))return 0;
    wx_vfs_entry hello={0},status={0};
    if(!wx_fat_lookup(&wx_root_volume,"HELLO.TXT",&hello) ||
       !wx_fat_lookup(&wx_root_volume,"status.txt",&status) ||
       wx_fat_lookup(&wx_root_volume,"../HELLO.TXT",&status) ||
       hello.size!=900 || !wx_vfs_read_file("HELLO.TXT",wx_vfs_scratch,sizeof(wx_vfs_scratch),&hello.size))return 0;
    static const char msg[]="WINUX FAT16 FILE OK: scattered cluster chain 2 -> 4;";
    for(unsigned i=0;i<sizeof(msg)-1;i++)if(wx_vfs_scratch[i]!=msg[i])return 0;
    if(wx_vfs_scratch[899]=='\0')return 0;
    u32 length=0;
    if(!wx_vfs_read_file("STATUS.TXT",wx_vfs_scratch,sizeof(wx_vfs_scratch),&length) ||
       length!=28)return 0;
    static const char answer[]="WINUX_FAT16_RUNTIME_READ_OK\n";
    for(unsigned i=0;i<sizeof(answer)-1;i++)if(wx_vfs_scratch[i]!=answer[i])return 0;
    print("VFS_FAT16_OK: readonly mount, 2 root files, noncontiguous 900-byte cluster chain, safe path rejection\n");
    return 1;
}
#endif
