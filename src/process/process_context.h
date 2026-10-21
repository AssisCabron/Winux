/* Single-thread publication. Environment buffers never move during a process.
 * Existing console handles remain runtime handles, not general NT objects. */
static int process_env_order(const u16 *a, const u16 *b) {
    for (unsigned i = 0; i < 64; i++) {
        u16 x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z')x += 32;
        if (y >= 'A' && y <= 'Z')y += 32;
        if (x != y)return x < y ? -1 : 1;
        if (!x)return 0;
    }
    return 0;
}

static void process_environment_sync(void) {
    unsigned order[16], count = 0;
    for (unsigned i = 0; i < 16; i++)
        if (app_env[i].used) {
            unsigned j = count++;
            while (j && process_env_order(app_env[i].name, app_env[order[j - 1]].name) < 0) {
                order[j] = order[j - 1];
                j--;
            }
            order[j] = i;
        }
    zero_bytes(app_environment, sizeof(app_environment));
    unsigned at = 0;
    for (unsigned k = 0; k < count; k++) {
        env_entry *e = &app_env[order[k]];
        for (unsigned i = 0; i < 63 && e->name[i]; i++)app_environment[at++] = e->name[i];
        app_environment[at++] = '=';
        for (unsigned i = 0; i < 255 && e->value[i]; i++)app_environment[at++] = e->value[i];
        at++;
    }
    at++;
    if (at < 2)at = 2;
    app_environment_bytes = (at * 2ULL + 7) & ~7ULL;
    pp_put64(app_parameters + 0x80, (u64) app_environment);
    pp_put64(app_parameters + 0x3f0, app_environment_bytes);
}

static pp_string process_string(const u16 *s) {
    unsigned n = 0;
    while (s[n])n++;
    return (pp_string)
    {
        (u16)(n * 2), (u16)(n * 2 + 2), s
    };
}

static int process_context_build(void) {
    static const u16 dll_path[] = {
        'C', ':', '\\', 'W', 'i', 'n', 'd', 'o', 'w', 's', '\\', 'S', 'y', 's', 't', 'e', 'm', '3', '2', 0
    };
    static const u16 directory[] = {
        'C', ':', '\\', 'W', 'i', 'n', 'd', 'o', 'w', 's', '\\', 'S', 'y', 's', 't', 'e', 'm', '3', '2', '\\', 0
    };
    static const u16 empty[] = {0, 0};
    pp_input input = {0};
    size_t required = 0;
    for (unsigned i = 0; i < 8; i++)input.strings[i] = process_string(empty);
    input.strings[0] = input.strings[3] = process_string(app_cmdline);
    input.strings[1] = process_string(dll_path);
    input.strings[2] = process_string(directory);
    input.environment = empty;
    input.environment_bytes = 4;
    input.flags = 1;
    input.standard_handles[0] = 3;
    input.standard_handles[1] = 1;
    input.standard_handles[2] = 2;
    if (pp_build(app_parameters, sizeof(app_parameters), &input, &required))return 0;
    process_environment_sync();
    pp_put64(app_peb + 0x20, (u64) app_parameters);
    pp_put32(app_peb + 0xb8, 1); /* Current guest has exactly one processor. */
    return 1;
}
