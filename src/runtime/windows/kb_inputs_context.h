/* Pinned KernelBase startup inputs; caller must hold the native GS scope.
 * This prepares observed producer state, not the full DLL process attach. */
typedef struct {
    u16 length, maximum;
    u32 padding;
    u8 *data;
} nt_ansi_string;

static struct {
    pe_module *module;
    nt_ansi_string command;

    void (*release)(nt_ansi_string *);

    u8 previous_command[16], previous_version[16], previous_selection[0x38];
    u64 previous_cached_version;
    u8 previous_system_basic[0x40];
} kb_inputs;

static int kb_inputs_stop(void) {
    pe_module *k = kb_inputs.module;
    if (!k)return 1;
    if (k->state != 2 || rd64(k->base + 0x2b6f58) != (u64) kb_inputs.command.data)return 0;
    /* Keep the heap and NLS alive until this owned allocation is released. */
    kb_inputs.release(&kb_inputs.command);
    if (kb_inputs.command.data)return 0;
    for (unsigned i = 0; i < 16; i++) {
        k->base[0x2b6f50 + i] = kb_inputs.previous_command[i];
        app_peb[0x118 + i] = kb_inputs.previous_version[i];
    }
    for (unsigned i = 0; i < 0x38; i++)k->base[0x2b60f0 + i] = kb_inputs.previous_selection[i];
    pp_put64(k->base + 0x2b8558, kb_inputs.previous_cached_version);
    for (unsigned i = 0; i < 0x40; i++)k->base[0x2b6d60 + i] = kb_inputs.previous_system_basic[i];
    zero_bytes(&kb_inputs, sizeof(kb_inputs));
    return 1;
}

static int kb_inputs_start(pe_module *n) {
    pe_module *k = module_named("kernelbase.dll");
    if (kb_inputs.module || !k || k->state != 2 || k->size < 0x2b8560 || rd64(k->base + 0x2b6f58) ||
        rd64(app_peb + 0x2d8) || rd64(app_peb + 0x2f0))
        return 0;
    u32(*convert)(nt_ansi_string *, const pp_string *, u8) = (void *) module_export(
        n, "RtlUnicodeStringToAnsiString", 0, 0);
    void (*release)(nt_ansi_string *) = (void *) module_export(n, "RtlFreeAnsiString", 0, 0);
    u32(*query)(u32, void*, u32, u32 *) = (void *) module_export(n, "NtQuerySystemInformation", 0, 0);
    if (!convert || !release || !query || module_export(k, "GetCommandLineA", 0, 0) != (u64) k->base + 0x7cb40)return 0;
    /* KernelBase process-init produces this block through class 0. Query into
     * temporary storage first so failure cannot publish partial global state. */
    u8 system_basic[0x40];
    zero_bytes(system_basic, sizeof(system_basic));
    u32 returned = 0;
    if (query(0, system_basic, sizeof(system_basic), &returned) || returned != sizeof(system_basic) ||
        rd32(system_basic + 8) != 4096)return 0;
    pp_string source = {
        rd16(app_parameters + 0x70), rd16(app_parameters + 0x72), (const u16 *) rd64(app_parameters + 0x78)
    };
    if (source.length > source.maximum || (source.length & 1) || !nt_obj_readable(source.data, source.length))return 0;
    nt_ansi_string command = {0};
    u32 status = convert(&command, &source, 1);
    if (status || !command.data) {
        if (command.data)release(&command);
        return 0;
    }
    /* Publish only after conversion has succeeded; partial allocation failure
     * leaves the old PEB and KernelBase command line untouched. */
    for (unsigned i = 0; i < 16; i++) {
        kb_inputs.previous_command[i] = k->base[0x2b6f50 + i];
        kb_inputs.previous_version[i] = app_peb[0x118 + i];
    }
    for (unsigned i = 0; i < 0x38; i++)kb_inputs.previous_selection[i] = k->base[0x2b60f0 + i];
    kb_inputs.previous_cached_version = rd64(k->base + 0x2b8558);
    for (unsigned i = 0; i < 0x40; i++) {
        kb_inputs.previous_system_basic[i] = k->base[0x2b6d60 + i];
        k->base[0x2b6d60 + i] = system_basic[i];
    }
    kb_inputs.command = command;
    kb_inputs.release = release;
    kb_inputs.module = k;
    for (unsigned i = 0; i < 16; i++)k->base[0x2b6f50 + i] = ((const u8 *) &command)[i];
    /* Declared experimental ABI profile; not a claim of Windows equivalence. */
    zero_bytes(app_peb + 0x118, 16);
    pp_put32(app_peb + 0x118, 10);
    pp_put16(app_peb + 0x120, 19041);
    pp_put32(app_peb + 0x124, 2);
    return 1;
}
