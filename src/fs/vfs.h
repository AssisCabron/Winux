/* Single mounted read-only volume interface backed by wx_block_device.
 * This is a VFS entry point, not a second physical disk driver.
 * Future FAT32/ext/NTFS implementations may bind the same operations. */
#ifndef WINUX_VFS_H
#define WINUX_VFS_H
#define WX_VFS_MAX_READ 2048U
typedef struct {char name[13];u32 size,first_cluster;} wx_vfs_entry;
typedef struct wx_vfs_volume wx_vfs_volume;
typedef int (*wx_vfs_lookup_fn)(wx_vfs_volume*,const char*,wx_vfs_entry*);
typedef int (*wx_vfs_read_fn)(wx_vfs_volume*,const wx_vfs_entry*,u8*,u32);
typedef int (*wx_vfs_list_fn)(wx_vfs_volume*);
struct wx_vfs_volume {
    const char *type;
    void *backend;
    wx_block_device *device;
    wx_vfs_lookup_fn lookup;
    wx_vfs_read_fn read;
    wx_vfs_list_fn list;
};
static wx_vfs_volume wx_root_volume;
static int wx_vfs_mount(const wx_vfs_volume *volume){
    if(!volume||!volume->type||!volume->backend||
       !volume->lookup||!volume->read||!volume->list)return 0;
    wx_root_volume=*volume;
    return 1;
}
static void wx_vfs_unmount_device(const wx_block_device *device){
    if(device && wx_root_volume.device==device)
        wx_root_volume=(wx_vfs_volume){0};
}
static int wx_vfs_read_file(const char *name,u8 *out,u32 capacity,u32 *size){
    if(!wx_root_volume.type||!name||!out||!size||!capacity)return 0;
    wx_vfs_entry entry={0};
    if(!wx_root_volume.lookup(&wx_root_volume,name,&entry) ||
       entry.size>capacity || entry.size>WX_VFS_MAX_READ)return 0;
    if(!wx_root_volume.read(&wx_root_volume,&entry,out,capacity))return 0;
    *size=entry.size;
    return 1;
}
#endif
