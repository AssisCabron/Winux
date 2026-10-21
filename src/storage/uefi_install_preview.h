/* UEFI-only first boot setup for an explicitly tagged Winux FAT data volume.
 * Never writes an untagged volume and never formats a disk. All handles close
 * before ExitBootServices. No UEFI runtime filesystem access is claimed. */
#define WX_EFI_FILE_READ 1ULL
#define WX_EFI_FILE_WRITE 2ULL
#define WX_EFI_FILE_CREATE 0x8000000000000000ULL
#define WX_EFI_FILE_DIRECTORY 0x10ULL

typedef struct wx_uefi_file {
    u64 revision;
    void *open, *close, *delete_file, *read, *write, *get_position,
        *set_position, *get_info, *set_info, *flush;
} wx_uefi_file;
typedef struct { u64 revision; void *open_volume; } wx_uefi_sfs;
static u32 wx_storage_volume_found,wx_storage_installed,wx_storage_existing,wx_storage_failed;
typedef status (*wx_file_open_fn)(wx_uefi_file*,wx_uefi_file**,const u16*,u64,u64);
typedef status (*wx_file_close_fn)(wx_uefi_file*);
typedef status (*wx_file_read_fn)(wx_uefi_file*,u64*,void*);
typedef status (*wx_file_write_fn)(wx_uefi_file*,u64*,void*);

static void wx_file_close(wx_uefi_file *file){
    if(file)((wx_file_close_fn)file->close)(file);
}
static int wx_storage_dir(wx_uefi_file *root,const u16 *path) {
    wx_uefi_file *out=0;
    status s=((wx_file_open_fn)root->open)(root,&out,path,
        WX_EFI_FILE_READ|WX_EFI_FILE_WRITE,0);
    if(s) s=((wx_file_open_fn)root->open)(root,&out,path,
        WX_EFI_FILE_READ|WX_EFI_FILE_WRITE|WX_EFI_FILE_CREATE,WX_EFI_FILE_DIRECTORY);
    if(s||!out)return 0;
    wx_file_close(out);return 1;
}
static int wx_storage_create_file(wx_uefi_file *root,const u16 *path,
                                   const char *contents,u64 size,int *existed) {
    wx_uefi_file *out=0;
    if(!((wx_file_open_fn)root->open)(root,&out,path,WX_EFI_FILE_READ,0)){
        if(existed)*existed=1;
        wx_file_close(out);
        return 1;
    }
    status s=((wx_file_open_fn)root->open)(root,&out,path,
        WX_EFI_FILE_READ|WX_EFI_FILE_WRITE|WX_EFI_FILE_CREATE,0);
    if(s||!out)return 0;
    u64 wrote=size;
    s=((wx_file_write_fn)out->write)(out,&wrote,(void*)contents);
    wx_file_close(out);
    return !s && wrote==size;
}
static int wx_storage_bootstrap(wx_uefi_file *root) {
    static const u16 *dirs[]={
        u"SYSTEM", u"SYSTEM\\CONFIG",u"SYSTEM\\DRIVERS",u"SYSTEM\\LIBS",
        u"SYSTEM\\LIBS\\WIN32",u"SYSTEM\\LIBS\\ELF",
        u"USERS",u"APPS",u"VAR",u"VAR\\LOG",u"VAR\\CACHE",u"TMP"
    };
    for(unsigned i=0;i<sizeof(dirs)/sizeof(dirs[0]);i++)
        if(!wx_storage_dir(root,dirs[i]))return 0;
    static const char manifest[]=
        "format=WINUX-CONFIG-1\nversion=0.38\n"
        "system=Winux\nsetup=required\n"
        "accounts=unprovisioned\n"
        "kernel=ring0\napplications=ring3\n";
    static const char user_manifest[]=
        "format=WINUX-USERS-1\n"
        "count=0\n"
        "setup=required\n"
        "# no accounts or passwords are preinstalled\n";
    int existing=0;
    if(!wx_storage_create_file(root,u"SYSTEM\\CONFIG\\SYSTEM.INI",
                                manifest,sizeof(manifest)-1,&existing))return 0;
    wx_storage_existing=existing;
    if(!wx_storage_create_file(root,u"SYSTEM\\CONFIG\\USERS.INI",
                                user_manifest,sizeof(user_manifest)-1,0))return 0;
    return 1;
}
static void wx_storage_install_uefi(boot_services *bs) {
    guid sfs_guid={0x964e5b22,0x6459,0x11d2,{0x8e,0x39,0,0xa0,0xc9,0x69,0x72,0x3b}};
    typedef status(*locate_handles_t)(u32,guid*,void*,u64*,void***);
    typedef status(*handle_protocol_t)(void*,guid*,void**);
    typedef status(*free_pool_t)(void*);
    u64 count=0;void **handles=0;
    status ret=((locate_handles_t)bs->fn[36])(2,&sfs_guid,0,&count,&handles);
    if(ret||!handles){serial("WINUX_STORAGE: NO_TAGGED_VOLUME\r\n");return;}
    if(count>32){wx_storage_failed=1;((free_pool_t)bs->fn[6])(handles);return;}
    for(u64 i=0;i<count;i++) {
        wx_uefi_sfs *sfs=0;
        if(((handle_protocol_t)bs->fn[16])(handles[i],&sfs_guid,(void**)&sfs)||!sfs)continue;
        wx_uefi_file *root=0;
        if(((status(*)(wx_uefi_sfs*,wx_uefi_file**))sfs->open_volume)(sfs,&root)||!root)continue;
        wx_uefi_file *marker=0;
        if(!((wx_file_open_fn)root->open)(root,&marker,u"WINUX.ID",WX_EFI_FILE_READ,0)&&marker) {
            char value[32]={0};u64 size=sizeof(value);
            status read_status=((wx_file_read_fn)marker->read)(marker,&size,value);
            wx_file_close(marker);
            static const char magic[]="WINUX-DATA-V1";
            int authorized=!read_status&&size==sizeof(magic)-1;
            if(authorized)for(unsigned j=0;j<sizeof(magic)-1;j++)
                if(value[j]!=magic[j])authorized=0;
            if(authorized) {
                wx_storage_volume_found=1;
                if(wx_storage_bootstrap(root)) {
                    wx_storage_installed=1;
                    serial(wx_storage_existing?"WINUX_STORAGE: EXISTING\r\n":
                                               "WINUX_STORAGE: INSTALLED\r\n");
                } else {
                    wx_storage_failed=1;
                    serial("WINUX_STORAGE: WRITE_FAILED\r\n");
                }
                wx_file_close(root);
                break;
            }
        }
        wx_file_close(root);
    }
    ((free_pool_t)bs->fn[6])(handles);
    if(!wx_storage_volume_found)serial("WINUX_STORAGE: NO_TAGGED_VOLUME\r\n");
}
