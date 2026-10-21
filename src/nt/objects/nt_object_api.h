/* Original NT adapters. Caller supplies guest-range validation and table.
 * Named creation, security descriptors and general object attributes unsupported.
 * Namespace must be configured consistently with the guest filesystem first. */
typedef struct {
    u16 length, maximum;
    u32 padding;
    u16 *buffer;
} nt_obj_string;

typedef struct {
    u32 length, padding;
    void *root;
    nt_obj_string *name;
    u32 attributes, padding2;
    void *security, *qos;
} nt_obj_attributes;

_Static_assert (
sizeof
(nt_obj_string)
==
16
,
"NT string x64 layout"
);
_Static_assert (
sizeof
(nt_obj_attributes)
==
48
,
"NT attributes x64 layout"
);

static u32 nt_obj_attrs(const nt_obj_attributes *a, int named) {
    if (!a)return named ? OBJ_INVALID_PARAMETER : 0;
    if (!nt_obj_readable(a, sizeof(*a)))return 0xc0000005U;
    if (a->length != sizeof(*a))return OBJ_INVALID_PARAMETER;
    if (a->security || a->qos || (a->attributes & ~0x40U))return OBJ_NOT_SUPPORTED;
    if (!named)return a->root || a->name ? OBJ_NOT_SUPPORTED : 0;
    if (!a->name || !nt_obj_readable(a->name, sizeof(*a->name)))return 0xc0000005U;
    const nt_obj_string *s = a->name;
    if ((s->length & 1) || s->length > s->maximum || !s->length)return OBJ_INVALID_PARAMETER;
    if (!nt_obj_readable(s->buffer, s->length))return 0xc0000005U;
    return 0;
}

static u32 nt_CreateEvent(void **out, u32 access, nt_obj_attributes *a, u32 type, u8 initial) {
    if (!nt_obj_writable(out, 8))return 0xc0000005U;
    u32 s = nt_obj_attrs(a, 0);
    if (s)return s;
    uint64_t h = 0;
    s = obj_event_create(&nt_objects, access, type, initial, &h);
    if (!s)*out = (void *) h;
    return s;
}

static u32 nt_SetEvent(void *h, int32_t *previous) {
    if (previous && !nt_obj_writable(previous, 4))return 0xc0000005U;
    return obj_event_set(&nt_objects, (u64) h, previous);
}

static u32 nt_WaitForSingleObject(void *h, u8 alertable, const int64_t *timeout) {
    if (timeout && !nt_obj_readable(timeout, 8))return 0xc0000005U;
    return obj_event_wait(&nt_objects, (u64) h, alertable, timeout);
}

static u32 nt_Close(void *h) { return obj_close(&nt_objects, (u64) h); }

static u32 nt_obj_open(unsigned kind, void **out, u32 access, nt_obj_attributes *a) {
    if (!nt_obj_writable(out, 8))return 0xc0000005U;
    u32 s = nt_obj_attrs(a, 1);
    if (s)return s;
    uint64_t h = 0;
    s = obj_namespace_open(&nt_objects, kind, (u64) a->root, a->name->buffer, a->name->length, !!(a->attributes & 0x40),
                           access, &h);
    if (!s)*out = (void *) h;
    return s;
}

static u32 nt_OpenDirectoryObject(void **out, u32 access, nt_obj_attributes *a) {
    return nt_obj_open(2, out, access, a);
}

static u32 nt_OpenSymbolicLinkObject(void **out, u32 access, nt_obj_attributes *a) {
    return nt_obj_open(3, out, access, a);
}

static u32 nt_QuerySymbolicLinkObject(void *h, nt_obj_string *target, u32 *returned) {
    if (!nt_obj_writable(target, sizeof(*target)) || (returned && !nt_obj_writable(returned, 4)))return 0xc0000005U;
    if (target->maximum && !nt_obj_writable(target->buffer, target->maximum))return 0xc0000005U;
    return obj_link_query(&nt_objects, (u64) h, target->buffer, target->maximum, &target->length, returned);
}

static u64 nt_object_export(const char *name) {
    if (same(name, "NtCreateEvent"))return (u64) nt_CreateEvent;
    if (same(name, "NtSetEvent"))return (u64) nt_SetEvent;
    if (same(name, "NtWaitForSingleObject"))return (u64) nt_WaitForSingleObject;
    if (same(name, "NtClose"))return (u64) nt_Close;
    if (same(name, "NtOpenDirectoryObject"))return (u64) nt_OpenDirectoryObject;
    if (same(name, "NtOpenSymbolicLinkObject"))return (u64) nt_OpenSymbolicLinkObject;
    if (same(name, "NtQuerySymbolicLinkObject"))return (u64) nt_QuerySymbolicLinkObject;
    return 0;
}
