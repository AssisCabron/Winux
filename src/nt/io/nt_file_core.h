/* Bounded in-memory file objects. Caller owns backing storage and validates
 * buffers. Single-thread only; no async, filesystem names or console emulation. */
#ifndef WINUX_NT_FILE_CORE_H
#define WINUX_NT_FILE_CORE_H
#include "../objects/nt_objects.h"
#define OBJ_FILE_KIND 4
#define OBJ_ENDPOINT_KIND 5
#define OBJ_FILE_READ UINT32_C(1)
#define OBJ_FILE_WRITE UINT32_C(2)
#define OBJ_END_OF_FILE UINT32_C(0xc0000011)

typedef struct {
    uint8_t *data;
    uint64_t length, capacity;
} io_file;

typedef uint32_t (*io_transfer_fn)(void *, void *, uint32_t, uint64_t *);

typedef struct {
    void *context;
    io_transfer_fn read, write;
    uint32_t device, characteristics;
} io_endpoint;

typedef struct {
    uint32_t generation;
    io_file *file;
    uint64_t position;
    io_endpoint *endpoint;
} io_open_file;

typedef struct {
    io_open_file slots[OBJ_SLOTS];
} io_table;

static uint32_t io_file_open(obj_table *objects, io_table *io, io_file *file, uint32_t access, uint64_t *out) {
    if (!file || file->length > file->capacity || (!file->data && file->capacity))return OBJ_INVALID_PARAMETER;
    if (access & ~(OBJ_FILE_READ | OBJ_FILE_WRITE | OBJ_SYNCHRONIZE))return OBJ_NOT_SUPPORTED;
    uint64_t handle;
    uint32_t status = obj_allocate(objects, access,OBJ_FILE_KIND, 0, 0, &handle);
    if (status)return status;
    obj_slot *slot = obj_lookup(objects, handle);
    unsigned i = (unsigned) (slot - objects->slots);
    io->slots[i] = (io_open_file){slot->generation, file, 0, 0};
    *out = handle;
    return 0;
}

static uint32_t io_endpoint_open(obj_table *objects, io_table *io, io_endpoint *endpoint, uint32_t access,
                                 uint64_t *out) {
    if (!endpoint || ((access & OBJ_FILE_READ) && !endpoint->read) || ((access & OBJ_FILE_WRITE) && !endpoint->write))
        return OBJ_INVALID_PARAMETER;
    if (access & ~(OBJ_FILE_READ | OBJ_FILE_WRITE | OBJ_SYNCHRONIZE))return OBJ_NOT_SUPPORTED;
    uint64_t handle;
    uint32_t status = obj_allocate(objects, access,OBJ_ENDPOINT_KIND, 0, 0, &handle);
    if (status)return status;
    obj_slot *slot = obj_lookup(objects, handle);
    unsigned i = (unsigned) (slot - objects->slots);
    io->slots[i] = (io_open_file){slot->generation, 0, 0, endpoint};
    *out = handle;
    return 0;
}

static uint32_t io_file_resolve(obj_table *objects, io_table *io, uint64_t handle, uint32_t access,
                                io_open_file **out) {
    obj_slot *slot = obj_lookup(objects, handle);
    if (!slot)return OBJ_INVALID_HANDLE;
    if (slot->kind != OBJ_FILE_KIND && slot->kind != OBJ_ENDPOINT_KIND)return OBJ_TYPE_MISMATCH;
    if ((slot->access & access) != access)return OBJ_ACCESS_DENIED;
    io_open_file *open = &io->slots[slot - objects->slots];
    if (open->generation != slot->generation || (!open->file && !open->endpoint))return OBJ_INVALID_HANDLE;
    *out = open;
    return 0;
}

static uint32_t io_file_transfer(obj_table *objects, io_table *io, uint64_t handle, int write,
                                 void *buffer, uint32_t count, const int64_t *offset, uint64_t *transferred) {
    io_open_file *open;
    uint32_t status = io_file_resolve(objects, io, handle, write ? OBJ_FILE_WRITE : OBJ_FILE_READ, &open);
    if (status)return status;
    if (offset && *offset < 0)return OBJ_NOT_SUPPORTED; /* Special offsets not implemented. */
    if (count && !buffer)return OBJ_INVALID_PARAMETER;
    if (open->endpoint) {
        if (offset)return OBJ_NOT_SUPPORTED;
        if (!count) {
            *transferred = 0;
            return 0;
        }
        io_transfer_fn fn = write ? open->endpoint->write : open->endpoint->read;
        if (!fn)return OBJ_ACCESS_DENIED;
        return fn(open->endpoint->context, buffer, count, transferred);
    }
    uint64_t position = offset ? (uint64_t) * offset : open->position;
    io_file *file = open->file;
    if (!count) {
        *transferred = 0;
        return 0;
    } /* Keep current position unchanged. */
    if (write) {
        if (position > file->capacity || count > file->capacity - position)return OBJ_NOT_SUPPORTED;
        for (uint64_t i = file->length; i < position; i++)file->data[i] = 0;
        for (uint32_t i = 0; i < count; i++)file->data[position + i] = ((const uint8_t *) buffer)[i];
        open->position = position + count;
        if (file->length < open->position)file->length = open->position;
        *transferred = count;
        return 0;
    }
    if (position >= file->length) {
        *transferred = 0;
        return OBJ_END_OF_FILE;
    }
    uint64_t amount = file->length - position;
    if (amount > count)amount = count;
    for (uint64_t i = 0; i < amount; i++)((uint8_t *) buffer)[i] = file->data[position + i];
    open->position = position + amount;
    *transferred = amount;
    return 0;
}
#endif
