/* Version-bound KernelBase ACP/OEM ownership. Caller holds native scope.
 * Retained REA: kernelbase-codepage-init/cp-node/cp-node-free dossiers.
 * No DLL entry point, locale initialization or arbitrary codepage loading. */
static struct {
    pe_module *module;
    u64 acp, oem;
    u32 previous[4];
} kb_nls;

static int kb_nls_stop(void) {
    pe_module *k = kb_nls.module;
    if (!k)return 1;
    if (k->state != 2 || rd64(k->base + 0x2b8880) != kb_nls.acp || rd64(k->base + 0x2b88a0) != kb_nls.oem)return 0;
    if (kb_nls.oem && kb_nls.oem != kb_nls.acp)((void(*)(void *)) (k->base + 0xf2a5c))((void *) kb_nls.oem);
    if (kb_nls.acp)((void(*)(void *)) (k->base + 0xf2a5c))((void *) kb_nls.acp);
    pp_put64(k->base + 0x2b8880, 0);
    pp_put64(k->base + 0x2b88a0, 0);
    const u32 offsets[] = {0x2b8200, 0x2b8204, 0x2b81fc, 0x2b84e0};
    for (unsigned i = 0; i < 4; i++)pp_put32(k->base + offsets[i], kb_nls.previous[i]);
    zero_bytes(&kb_nls, sizeof(kb_nls));
    return 1;
}

static int kb_nls_start(void) {
    pe_module *k = module_named("kernelbase.dll");
    if (kb_nls.module || !k || k->state != 2 || k->size < 0x2b88a8 || rd64(k->base + 0x2b8880) || rd64(
            k->base + 0x2b88a0))return 0;
    if (module_export(k, "WideCharToMultiByte", 0, 0) != (u64) k->base + 0x4bd20)return 0;
    const u32 offsets[] = {0x2b8200, 0x2b8204, 0x2b81fc, 0x2b84e0};
    for (unsigned i = 0; i < 4; i++)kb_nls.previous[i] = rd32(k->base + offsets[i]);
    kb_nls.module = k;
    int status = ((int(*)(void)) (k->base + 0x4abf8))();
    kb_nls.acp = rd64(k->base + 0x2b8880);
    kb_nls.oem = rd64(k->base + 0x2b88a0);
    if (status || !kb_nls.acp || !kb_nls.oem || rd32(k->base + 0x2b8200) != 1252 || rd32(k->base + 0x2b8204) != 850) {
        kb_nls_stop();
        return 0;
    }
    return 1;
}
