/* Original object/event core. Single CPU, caller serialized; no scheduler yet.
 * NT adapters must validate guest pointers before calling this core.
 * Handles are generation-tagged and never reused after generation exhaustion.
 * Guest adapters and syscall routes exist; VM integration tests are pending. */
#ifndef WINUX_NT_OBJECTS_H
#define WINUX_NT_OBJECTS_H
#include <stdint.h>
#define OBJ_SLOTS 64
#define OBJ_INVALID_HANDLE UINT32_C(0xc0000008)
#define OBJ_ACCESS_DENIED UINT32_C(0xc0000022)
#define OBJ_INVALID_PARAMETER UINT32_C(0xc000000d)
#define OBJ_NOT_SUPPORTED UINT32_C(0xc00000bb)
#define OBJ_NO_MEMORY UINT32_C(0xc0000017)
#define OBJ_TIMEOUT UINT32_C(0x102)
#define OBJ_EVENT_ALL UINT32_C(0x1f0003)
#define OBJ_SYNCHRONIZE UINT32_C(0x100000)
#define OBJ_EVENT_MODIFY UINT32_C(2)
#define OBJ_TYPE_MISMATCH UINT32_C(0xc0000024)
#define OBJ_NAME_NOT_FOUND UINT32_C(0xc0000034)
#define OBJ_BUFFER_TOO_SMALL UINT32_C(0xc0000023)

typedef struct {
    uint32_t generation, access;
    uint8_t kind, event_type, signaled, retired;
} obj_slot;

typedef struct {
    obj_slot slots[OBJ_SLOTS];
    uint16_t dll_path[260], dll_path_bytes;
} obj_table;

/* Discard process-owned objects, retaining generations to reject old handles. */
static void obj_reset(obj_table *t) {
    for (unsigned i = 0; i < OBJ_SLOTS; i++) {
        t->slots[i].kind = 0;
        t->slots[i].access = 0;
        t->slots[i].signaled = 0;
    }
}

static uint64_t obj_handle(unsigned index, uint32_t generation) {
    return ((uint64_t) generation << 16) | UINT64_C(0x8000) | ((uint64_t) index << 2);
}

static obj_slot *obj_lookup(obj_table *t, uint64_t handle) {
    if ((handle & UINT64_C(0xff03)) != UINT64_C(0x8000) || (handle >> 48))return 0;
    unsigned index = (unsigned) ((handle >> 2) & 63);
    obj_slot *s = &t->slots[index];
    if (!s->kind || s->generation != (uint32_t)(handle >> 16))return 0;
    return s;
}

static uint32_t obj_allocate(obj_table *t, uint32_t access, uint8_t kind, uint8_t type, int initial, uint64_t *out) {
    for (unsigned i = 0; i < OBJ_SLOTS; i++) {
        obj_slot *s = &t->slots[i];
        if (s->kind || s->retired)continue;
        if (s->generation == UINT32_MAX) {
            s->retired = 1;
            continue;
        }
        ++s->generation;
        s->access = access;
        s->event_type = (uint8_t) type;
        s->signaled = initial != 0;
        s->kind = kind;
        *out = obj_handle(i, s->generation);
        return 0;
    }
    return OBJ_NO_MEMORY;
}

static uint32_t obj_event_create(obj_table *t, uint32_t access, uint32_t type, int initial, uint64_t *out) {
    if (type > 1)return OBJ_INVALID_PARAMETER;
    /* Generic/MAXIMUM_ALLOWED/security mappings belong in the future adapter. */
    if (access & ~OBJ_EVENT_ALL)return OBJ_NOT_SUPPORTED;
    return obj_allocate(t, access, 1, (uint8_t) type, initial, out);
}

static uint32_t obj_close(obj_table *t, uint64_t handle) {
    obj_slot *s = obj_lookup(t, handle);
    if (!s)return OBJ_INVALID_HANDLE;
    s->kind = 0;
    s->access = 0;
    s->signaled = 0;
    return 0;
}

static uint32_t obj_event_set(obj_table *t, uint64_t handle, int32_t *previous) {
    obj_slot *s = obj_lookup(t, handle);
    if (!s)return OBJ_INVALID_HANDLE;
    if (s->kind != 1)return OBJ_TYPE_MISMATCH;
    if (!(s->access & OBJ_EVENT_MODIFY))return OBJ_ACCESS_DENIED;
    if (previous)*previous = s->signaled;
    s->signaled = 1;
    return 0;
}

/* Only zero-timeout polling is implemented. Never claim a blocking wait passed. */
static uint32_t obj_event_wait(obj_table *t, uint64_t handle, int alertable, const int64_t *timeout) {
    obj_slot *s = obj_lookup(t, handle);
    if (!s)return OBJ_INVALID_HANDLE;
    if (s->kind != 1)return OBJ_TYPE_MISMATCH;
    if (!(s->access & OBJ_SYNCHRONIZE))return OBJ_ACCESS_DENIED;
    if (alertable || !timeout || *timeout != 0)return OBJ_NOT_SUPPORTED;
    if (!s->signaled)return OBJ_TIMEOUT;
    if (s->event_type == 1)s->signaled = 0;
    return 0;
}

/* Configure guest namespace before exposing handles. Never reads host paths. */
static uint32_t obj_configure_dll_path(obj_table *t, const uint16_t *path, unsigned bytes) {
    if (!path || !bytes || (bytes & 1) || bytes > 518)return OBJ_INVALID_PARAMETER;
    for (unsigned i = 0; i < OBJ_SLOTS; i++)if (t->slots[i].kind)return OBJ_NOT_SUPPORTED;
    for (unsigned i = 0; i < bytes / 2; i++)if (!path[i])return OBJ_INVALID_PARAMETER;
    for (unsigned i = 0; i < bytes / 2; i++)t->dll_path[i] = path[i];
    t->dll_path[bytes / 2] = 0;
    t->dll_path_bytes = (uint16_t) bytes;
    return 0;
}

static int obj_name(const uint16_t *name, unsigned bytes, const char *literal, int insensitive) {
    if (bytes & 1)return 0;
    unsigned i = 0;
    for (; literal[i]; i++) {
        if (i >= bytes / 2)return 0;
        unsigned a = name[i], b = (unsigned char) literal[i];
        /* Only these fixed ASCII object names are supported, not Unicode folding. */
        if (insensitive) {
            if (a >= 'A' && a <= 'Z')a += 32;
            if (b >= 'A' && b <= 'Z')b += 32;
        }
        if (a != b)return 0;
    }
    return i == bytes / 2;
}

static uint32_t obj_namespace_open(obj_table *t, unsigned kind, uint64_t root, const uint16_t *name, unsigned bytes,
                                   int insensitive, uint32_t access, uint64_t *out) {
    if (kind != 2 && kind != 3)return OBJ_INVALID_PARAMETER;
    if (!name || !bytes || (bytes & 1))return OBJ_INVALID_PARAMETER;
    if (access & ~(kind == 2 ? UINT32_C(0xf000f) : UINT32_C(0xf0001)))return OBJ_NOT_SUPPORTED;
    unsigned found = 0;
    if (root) {
        obj_slot *s = obj_lookup(t, root);
        if (!s)return OBJ_INVALID_HANDLE;
        if (s->kind != 2)return OBJ_TYPE_MISMATCH;
        if (!(s->access & 2))return OBJ_ACCESS_DENIED;
        if (obj_name(name, bytes, "KnownDllPath", insensitive))found = 3;
    } else {
        if (obj_name(name, bytes, "\\KnownDlls", insensitive))found = 2;
        if (obj_name(name, bytes, "\\KnownDlls\\KnownDllPath", insensitive))found = 3;
    }
    if (!found || !t->dll_path_bytes)return OBJ_NAME_NOT_FOUND;
    if (kind != found)return OBJ_TYPE_MISMATCH;
    return obj_allocate(t, access, (uint8_t) kind, 0, 0, out);
}

static uint32_t obj_link_query(obj_table *t, uint64_t handle, uint16_t *buffer, unsigned capacity, uint16_t *length,
                               uint32_t *required) {
    obj_slot *s = obj_lookup(t, handle);
    if (!s)return OBJ_INVALID_HANDLE;
    if (s->kind != 3)return OBJ_TYPE_MISMATCH;
    if (!(s->access & 1))return OBJ_ACCESS_DENIED;
    unsigned need = t->dll_path_bytes + 2;
    if (required)*required = need;
    if (capacity < need)return OBJ_BUFFER_TOO_SMALL;
    if (!buffer)return OBJ_INVALID_PARAMETER;
    for (unsigned i = 0; i < need / 2; i++)buffer[i] = t->dll_path[i];
    *length = t->dll_path_bytes;
    return 0;
}
#endif
