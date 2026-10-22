/* FAT16 read-only filesystem over wx_block_read; safe bounded 8.3 tree.
 * No second block driver or VFS. Root + up to 3 path components, 8 KiB
 * files, 32 directory clusters and 512 entries per directory.
 * Malformed FAT chains, cycles and directory traversal are rejected.
 */
#ifndef WINUX_FAT16_RO_H
#define WINUX_FAT16_RO_H

typedef struct {
    wx_block_device *disk;
    u32 reserved, fat_size, first_root, root_sectors, first_data;
    u32 total_sectors, data_clusters, root_entries, spc;
} wx_fat16_mount;

static wx_fat16_mount wx_fat16_ctx;
static u8 wx_fat16_sector[512];
static u8 wx_vfs_scratch[WX_VFS_MAX_READ];
static u16 wx_fat_get16(const u8 *p) { return (u16) p[0] | ((u16) p[1] << 8); }
static u32 wx_fat_get32(const u8 *p) { return (u32) wx_fat_get16(p) | ((u32) wx_fat_get16(p + 2) << 16); }

static int wx_fat_disk_read(wx_fat16_mount *f, u32 lba, u8 *dest) {
    return f && f->disk && lba < f->total_sectors && wx_block_read(f->disk, lba, 1, dest);
}

static int wx_fat_name(const char *path, u8 out[11]) {
    if (!path || !*path)return 0;
    for (unsigned i = 0; i < 11; i++)out[i] = ' ';
    unsigned name = 0, ext = 0, in_ext = 0;
    for (const char *p = path; *p; p++) {
        unsigned char c = (unsigned char) *p;
        if (c == '.' && !in_ext && name) {
            in_ext = 1;
            continue;
        }
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-'))
            return 0;
        if (c >= 'a' && c <= 'z')c -= 32;
        if (!in_ext) {
            if (name == 8)return 0;
            out[name++] = c;
        } else {
            if (ext == 3)return 0;
            out[8 + ext++] = c;
        }
    }
    return name > 0 && (!in_ext || ext > 0);
}

static int wx_fat_next_cluster(wx_fat16_mount *f, u32 cluster, u32 *next) {
    if (!f || !next || cluster < 2 || cluster >= f->data_clusters + 2)return 0;
    u32 offset = cluster * 2;
    if (offset / 512 >= f->fat_size ||
        !wx_fat_disk_read(f, f->reserved + offset / 512, wx_fat16_sector))
        return 0;
    *next = wx_fat_get16(wx_fat16_sector + offset % 512);
    return 1;
}

static void wx_fat_decode(const u8 *e, wx_vfs_entry *found) {
    for (unsigned i = 0; i < 13; i++)found->name[i] = 0;
    unsigned off = 0;
    for (unsigned i = 0; i < 8 && e[i] != ' '; i++)found->name[off++] = (char) e[i];
    if (e[8] != ' ') {
        found->name[off++] = '.';
        for (unsigned i = 8; i < 11 && e[i] != ' '; i++)found->name[off++] = (char) e[i];
    }
    found->name[off] = 0;
    found->first_cluster = wx_fat_get16(e + 26);
    found->size = wx_fat_get32(e + 28);
    found->attr = e[11];
}

/* Return disk sector containing directory entry index. A cluster chain is
 * walked only inside the selected directory, with a hard traversal limit.
 * A cyclic chain cannot make this loop unbounded. */
static int wx_fat_dir_lba(wx_fat16_mount *f, u32 directory, u32 index, u32 *lba) {
    if (!f || !lba || index >= 512)return 0;
    if (!directory) {
        if (index >= f->root_entries)return 0;
        *lba = f->first_root + index / 16;
        return *lba < f->first_data;
    }
    if (f->spc != 1)return 0;
    u32 cluster = directory, skip = index / 16;
    if (skip > 32)return 0;
    u16 visited[34] = {0};
    unsigned used = 0;
    for (u32 n = 0; n < skip; n++) {
        if (cluster < 2 || cluster >= f->data_clusters + 2)return 0;
        for (unsigned i = 0; i < used; i++)if (visited[i] == cluster)return 0;
        visited[used++] = (u16) cluster;
        u32 next;
        if (!wx_fat_next_cluster(f, cluster, &next) ||
            next >= 0xfff8 || next < 2 || next >= f->data_clusters + 2)
            return 0;
        cluster = next;
    }
    *lba = f->first_data + (cluster - 2);
    return *lba < f->total_sectors;
}

static int wx_fat_dir_find(wx_fat16_mount *f, u32 directory,
                           const u8 query[11], wx_vfs_entry *result) {
    for (u32 n = 0; n < 512; n++) {
        u32 lba;
        if (!wx_fat_dir_lba(f, directory, n, &lba))return 0;
        if (!wx_fat_disk_read(f, lba, wx_fat16_sector))return 0;
        const u8 *e = wx_fat16_sector + (n % 16) * 32;
        if (!e[0])return 0;
        if (e[0] == 0xe5 || (e[11] & 0x08) || (e[11] & 0x0f) == 0x0f)continue;
        int match = 1;
        for (unsigned i = 0; i < 11; i++)if (e[i] != query[i])match = 0;
        if (!match)continue;
        wx_fat_decode(e, result);
        return result->first_cluster >= 2 &&
               result->first_cluster < f->data_clusters + 2;
    }
    return 0;
}

static int wx_fat_lookup(wx_vfs_volume *volume, const char *path, wx_vfs_entry *found) {
    wx_fat16_mount *f = (wx_fat16_mount *) volume->backend;
    if (!f || !path || !found)return 0;
    unsigned length = 0;
    while (path[length] && length < 48)length++;
    if (!length || length >= 48)return 0;
    u32 dir = 0;
    unsigned offset = 0;
    for (unsigned depth = 0; depth < 3; depth++) {
        char component[13] = {0};
        unsigned n = 0;
        while (offset < length && path[offset] != '/') {
            if (n >= 12)return 0;
            component[n++] = path[offset++];
        }
        if (!n)return 0;
        u8 query[11];
        if (!wx_fat_name(component, query) ||
            !wx_fat_dir_find(f, dir, query, found))
            return 0;
        if (offset == length)return 1;
        if (!(found->attr & 0x10))return 0;
        dir = found->first_cluster;
        offset++;
        if (offset >= length)return 0;
    }
    return 0;
}

static int wx_fat_read(wx_vfs_volume *volume, const wx_vfs_entry *file,
                       u8 *out, u32 capacity) {
    wx_fat16_mount *f = (wx_fat16_mount *) volume->backend;
    if (!f || !file || !out || (file->attr & 0x10) ||
        !file->size || file->size > capacity || file->size > WX_VFS_MAX_READ)
        return 0;
    u32 remain = file->size, cluster = file->first_cluster, pos = 0;
    u32 cluster_limit = (file->size + f->spc * 512 - 1) / (f->spc * 512);
    if (!cluster_limit || cluster_limit > WX_VFS_MAX_READ / 512)return 0;
    u16 visited[WX_VFS_MAX_READ / 512] = {0};
    for (u32 count = 0; count < cluster_limit; count++) {
        if (cluster < 2 || cluster >= f->data_clusters + 2)return 0;
        for (u32 i = 0; i < count; i++)if (visited[i] == cluster)return 0;
        visited[count] = (u16) cluster;
        u32 lba = f->first_data + (cluster - 2) * f->spc;
        for (u32 sec = 0; sec < f->spc && remain; sec++) {
            if (!wx_fat_disk_read(f, lba + sec, wx_fat16_sector))return 0;
            u32 n = remain < 512 ? remain : 512;
            for (u32 i = 0; i < n; i++)out[pos + i] = wx_fat16_sector[i];
            pos += n;
            remain -= n;
        }
        if (!remain)return 1;
        u32 next = 0;
        if (!wx_fat_next_cluster(f, cluster, &next) ||
            next >= 0xfff8 || next < 2 || next >= f->data_clusters + 2)
            return 0;
        cluster = next;
    }
    return 0;
}

static int wx_fat_list(wx_vfs_volume *volume, const char *path) {
    wx_fat16_mount *f = (wx_fat16_mount *) volume->backend;
    if (!f)return 0;
    u32 directory = 0;
    if (path && *path) {
        wx_vfs_entry dir = {0};
        if (!wx_fat_lookup(volume, path, &dir) || !(dir.attr & 0x10))return 0;
        directory = dir.first_cluster;
    }
    print("VFS_LIST_BEGIN\n");
    unsigned listed = 0;
    for (u32 n = 0; n < 512; n++) {
        u32 lba;
        if (!wx_fat_dir_lba(f, directory, n, &lba))break;
        if (!wx_fat_disk_read(f, lba, wx_fat16_sector))return 0;
        const u8 *e = wx_fat16_sector + (n % 16) * 32;
        if (!e[0])break;
        if (e[0] == 0xe5 || (e[11] & 0x08) || (e[11] & 0x0f) == 0x0f ||
            e[0] == '.')
            continue;
        wx_vfs_entry item = {0};
        wx_fat_decode(e, &item);
        print(item.attr & 0x10 ? "VFS_DIR " : "VFS_FILE ");
        print(item.name);
        if (!(item.attr & 0x10)) {
            print(" bytes=");
            number(item.size);
        }
        print("\n");
        listed++;
    }
    print("VFS_LIST_END\n");
    return listed > 0;
}

static int wx_fat_mount_volume(wx_block_device *device) {
    if (!device || !device->read || device->sector_bytes != 512)return 0;
    u8 b[512];
    if (!wx_block_read(device, 0, 1, b) || b[510] != 0x55 || b[511] != 0xaa)return 0;
    u32 bps = wx_fat_get16(b + 11), spc = b[13], reserved = wx_fat_get16(b + 14);
    u32 nfats = b[16], entries = wx_fat_get16(b + 17), fat_size = wx_fat_get16(b + 22);
    u32 total = wx_fat_get16(b + 19);
    if (!total)total = wx_fat_get32(b + 32);
    if (bps != 512 || spc != 1 || !reserved || !fat_size || nfats != 2 ||
        entries != 512 || total > device->capacity_sectors || total < 4096)
        return 0;
    u32 root_sectors = (entries * 32 + 511) / 512;
    u32 first_root = reserved + nfats * fat_size, first_data = first_root + root_sectors;
    if (first_data >= total)return 0;
    u32 clusters = (total - first_data) / spc;
    if (clusters < 4085 || clusters >= 65525 || fat_size * 512 < (clusters + 2) * 2)return 0;
    wx_fat16_ctx = (wx_fat16_mount){
        device, reserved, fat_size, first_root, root_sectors,
        first_data, total, clusters, entries, spc
    };
    wx_vfs_volume mount = {
        "FAT16-ro", &wx_fat16_ctx, device,
        wx_fat_lookup, wx_fat_read, wx_fat_list
    };
    return wx_vfs_mount(&mount);
}

static int wx_vfs_fat_test(void) {
    if (!wx_virtio_blk_detect() || wx_block_count != 1 ||
        !wx_fat_mount_volume(&wx_block_devices[0]))
        return 0;
    wx_vfs_entry hello = {0}, status = {0};
    if (!wx_fat_lookup(&wx_root_volume, "HELLO.TXT", &hello) ||
        !wx_fat_lookup(&wx_root_volume, "status.txt", &status) ||
        wx_fat_lookup(&wx_root_volume, "../HELLO.TXT", &status) ||
        hello.size != 900 ||
        !wx_vfs_read_file("HELLO.TXT", wx_vfs_scratch, sizeof(wx_vfs_scratch), &hello.size))
        return 0;
    static const char msg[] = "WINUX FAT16 FILE OK: scattered cluster chain 2 -> 4;";
    for (unsigned i = 0; i < sizeof(msg) - 1; i++)if (wx_vfs_scratch[i] != msg[i])return 0;
    if (wx_vfs_scratch[899] == '\0')return 0;
    u32 length = 0;
    if (!wx_vfs_read_file("STATUS.TXT", wx_vfs_scratch, sizeof(wx_vfs_scratch), &length) ||
        length != 28)
        return 0;
    static const char answer[] = "WINUX_FAT16_RUNTIME_READ_OK\n";
    for (unsigned i = 0; i < sizeof(answer) - 1; i++)if (wx_vfs_scratch[i] != answer[i])return 0;
    print("VFS_FAT16_OK: readonly mount, 2 root files, noncontiguous 900-byte cluster chain, safe path rejection\n");
    return 1;
}

static int wx_fat_tree_test(void) {
    if (!wx_root_volume.type)return 0;
    wx_vfs_entry entry = {0};
    if (!wx_fat_lookup(&wx_root_volume, "LOGS/REPORT.TXT", &entry) ||
        entry.size != 6144 || (entry.attr & 0x10))
        return 0;
    u32 length = 0;
    if (!wx_vfs_read_file("LOGS/REPORT.TXT", wx_vfs_scratch, sizeof(wx_vfs_scratch), &length)
        || length != 6144)
        return 0;
    for (u32 i = 0; i < length; i++)
        if (wx_vfs_scratch[i] != (u8)((i * 37 + 11) & 255))return 0;
    if (wx_vfs_read_file("LOGS/LOOP.TXT", wx_vfs_scratch, sizeof(wx_vfs_scratch), &length))
        return 0;
    if (!wx_fat_lookup(&wx_root_volume, "LOGS/ARCHIVE/NOTES.TXT", &entry) ||
        entry.size != 1536 ||
        !wx_vfs_read_file("LOGS/ARCHIVE/NOTES.TXT", wx_vfs_scratch, sizeof(wx_vfs_scratch), &length) ||
        length != 1536)
        return 0;
    for (u32 i = 0; i < length; i++)
        if (wx_vfs_scratch[i] != (u8)((i * 13 + 29) & 255))return 0;
    if (wx_fat_lookup(&wx_root_volume, "LOGS/../STATUS.TXT", &entry) ||
        wx_fat_lookup(&wx_root_volume, "LOGS//REPORT.TXT", &entry) ||
        wx_fat_lookup(&wx_root_volume, "LOGS/MISSING.TXT", &entry))
        return 0;
    print("VFS_TREE_OK: readonly subdirectory, 6144-byte noncontiguous FAT chain, loop/traversal rejected\n");
    return 1;
}
#endif
