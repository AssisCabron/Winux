/* Synchronous bounded-file NT adapters. Caller provides nt_objects, nt_io and
 * readable/writable validators. No completion is advertised for async requests. */
static void nt_io_put32(void *p, uint32_t value) {
    uint8_t *b = p;
    for (unsigned i = 0; i < 4; i++)b[i] = (uint8_t)(value >> (8 * i));
}

static void nt_io_put64(void *p, uint64_t value) {
    uint8_t *b = p;
    for (unsigned i = 0; i < 8; i++)b[i] = (uint8_t)(value >> (8 * i));
}

static uint32_t nt_file_transfer(void *handle, void *event, void *apc, void *context, void *iosb,
                                 void *buffer, uint32_t count, const int64_t *offset, const uint32_t *key, int write) {
    if (event || apc || context || key)return OBJ_NOT_SUPPORTED;
    if (!nt_obj_writable(iosb, 16))return UINT32_C(0xc0000005);
    if (count && !(write ? nt_obj_readable(buffer, count) : nt_obj_writable(buffer, count)))return UINT32_C(0xc0000005);
    int64_t captured = 0;
    if (offset) {
        if (!nt_obj_readable(offset, 8))return UINT32_C(0xc0000005);
        uint64_t value = 0;
        for (unsigned i = 0; i < 8; i++)value |= (uint64_t)((const uint8_t *) offset)[i] << (8 * i);
        captured = (int64_t) value;
    }
    uint64_t transferred = 0;
    uint32_t status = io_file_transfer(&nt_objects, &nt_io, (uint64_t) handle, write, buffer, count,
                                       offset ? &captured : 0, &transferred);
    /* Internal endpoint readiness is not an NT completion or STATUS_PENDING.
     * A hardware-backed synchronous endpoint must wait inside its callback. */
    if (status == UINT32_MAX)return OBJ_NOT_SUPPORTED;
    if (status && status != OBJ_END_OF_FILE)return status;
    /* Local synchronous-file profile: reads and zero-byte writes clear the union;
     * positive writes preserve its upper four bytes. See retained host fixture. */
    if (!write || !count)nt_io_put64(iosb, status);
    else nt_io_put32(iosb, status);
    nt_io_put64((uint8_t *) iosb + 8, transferred);
    return status;
}

static uint32_t nt_ReadFile(void *h, void *event, void *apc, void *context, void *iosb, void *buffer, uint32_t count,
                            const int64_t *offset, const uint32_t *key) {
    return nt_file_transfer(h, event, apc, context, iosb, buffer, count, offset, key, 0);
}

static uint32_t nt_WriteFile(void *h, void *event, void *apc, void *context, void *iosb, const void *buffer,
                             uint32_t count, const int64_t *offset, const uint32_t *key) {
    return nt_file_transfer(h, event, apc, context, iosb, (void *) buffer, count, offset, key, 1);
}

static uint32_t nt_QueryVolumeInformationFile(void *h, void *iosb, void *buffer, uint32_t count,
                                              uint32_t information_class) {
    if (information_class != 4)return OBJ_NOT_SUPPORTED;
    if (count < 8)return UINT32_C(0xc0000004);
    if (!nt_obj_writable(iosb, 16) || !nt_obj_writable(buffer, 8))return UINT32_C(0xc0000005);
    if (h == (void *) ~(uintptr_t) 0)return OBJ_TYPE_MISMATCH;
    io_open_file *open;
    uint32_t status = io_file_resolve(&nt_objects, &nt_io, (uint64_t) h, 0, &open);
    if (status)return status;
    nt_io_put32(buffer, open->endpoint ? open->endpoint->device : 7);
    nt_io_put32((uint8_t *) buffer + 4, open->endpoint ? open->endpoint->characteristics : 0);
    nt_io_put32(iosb, 0);
    nt_io_put64((uint8_t *) iosb + 8, 8);
    return 0;
}
