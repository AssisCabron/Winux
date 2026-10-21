/* Block device contract, independent of PCI/AHCI/NVMe/VirtIO and filesystems.
 * Read-only preview until per-device DMA safety, writable buffers, flush and
 * power-failure semantics can be proved. No second VFS or object registry. */
#ifndef WINUX_BLOCK_CORE_H
#define WINUX_BLOCK_CORE_H
#define WX_BLOCK_SECTOR_BYTES 512U
#define WX_BLOCK_MAX_DEVICES 8U
typedef struct wx_block_device wx_block_device;
typedef int (*wx_block_read_fn)(wx_block_device *device,u64 lba,u32 sectors,void *out);
struct wx_block_device {
    const char *driver;
    u64 capacity_sectors;
    u32 sector_bytes;
    u32 flags;
    void *backend;
    wx_block_read_fn read;
};
#define WX_BLOCK_FLAG_READ_ONLY 1U
static wx_block_device wx_block_devices[WX_BLOCK_MAX_DEVICES];
static u32 wx_block_count;

static int wx_block_register(const wx_block_device *device) {
    if (!device || !device->driver || !device->read ||
        device->sector_bytes != WX_BLOCK_SECTOR_BYTES ||
        !device->capacity_sectors || !(device->flags & WX_BLOCK_FLAG_READ_ONLY) ||
        wx_block_count == WX_BLOCK_MAX_DEVICES) return 0;
    wx_block_devices[wx_block_count++] = *device;
    return 1;
}
static int wx_block_read(wx_block_device *device,u64 lba,u32 sectors,void *out) {
    if (!device || !device->read || !out || !sectors || sectors > 128U ||
        lba >= device->capacity_sectors ||
        (u64)sectors > device->capacity_sectors-lba) return 0;
    return device->read(device,lba,sectors,out);
}
#endif
