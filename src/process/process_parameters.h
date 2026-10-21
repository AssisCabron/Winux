/* Original bounded bootstrap builder for the observed x64 parameter layout.
 * Internal API: caller supplies readable input spans and a writable output span.
 * No allocation, PEB publication, process launch, or host-pointer probing. */
#ifndef WINUX_PROCESS_PARAMETERS_H
#define WINUX_PROCESS_PARAMETERS_H
#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint16_t length, maximum;
    const uint16_t *data;
} pp_string;

typedef struct {
    pp_string strings[8]; /* image, DLL path, current directory, command, title, desktop, shell, runtime */
    const uint16_t *environment;
    size_t environment_bytes;
    uint64_t standard_handles[3];
    uint32_t console_flags, flags;
} pp_input;

#define PP_INVALID 0xc000000dU
#define PP_SMALL 0xc0000023U

static void pp_put16(uint8_t *p, uint16_t n) {
    p[0] = (uint8_t) n;
    p[1] = (uint8_t)(n >> 8);
}

static void pp_put32(uint8_t *p, uint32_t n) { for (unsigned i = 0; i < 4; i++)p[i] = (uint8_t)(n >> (8 * i)); }
static void pp_put64(uint8_t *p, uint64_t n) { for (unsigned i = 0; i < 8; i++)p[i] = (uint8_t)(n >> (8 * i)); }
static size_t pp_align(size_t n) { return (n + 7) & ~(size_t) 7; }

static int pp_overlap(const void *a, size_t an, const void *b, size_t bn) {
    uintptr_t x = (uintptr_t) a, y = (uintptr_t) b;
    if (an > UINTPTR_MAX - x || bn > UINTPTR_MAX - y)return 1;
    return an && bn && x < y + bn && y < x + an;
}

static uint32_t pp_build(void *output, size_t capacity, const pp_input *in, size_t *required) {
    if (!in || !required || in->flags > 1 || in->console_flags > 1)return PP_INVALID;
    uint16_t caps[8];
    size_t total = 0x440;
    for (unsigned i = 0; i < 8; i++) {
        pp_string s = in->strings[i];
        if ((s.length & 1) || (s.maximum & 1) || s.length > s.maximum || (s.length && !s.data))return PP_INVALID;
        caps[i] = s.maximum;
    }
    if (!in->strings[0].length || !in->strings[2].length)return PP_INVALID;
    if (in->strings[0].length > 65532)return PP_INVALID;
    caps[0] = (uint16_t)(in->strings[0].length + 2);
    if (in->strings[3].length != in->strings[3].maximum) {
        if (in->strings[3].length > 65532)return PP_INVALID;
        caps[3] = (uint16_t)(in->strings[3].length + 2);
    }
    pp_string directory = in->strings[2];
    unsigned add_slash = directory.data[directory.length / 2 - 1] != '\\';
    if ((unsigned) directory.length + add_slash * 2 > 518)return PP_INVALID;
    caps[2] = 520;
    if (!in->strings[7].length)caps[7] = 0;
    for (unsigned i = 0; i < 8; i++)total += pp_align(caps[i]);
    size_t eb = in->environment_bytes;
    if (!in->environment || eb < 4 || (eb & 1) || eb > 1024 * 1024)return PP_INVALID;
    size_t units = eb / 2;
    if (in->environment[units - 1] || in->environment[units - 2])return PP_INVALID;
    for (size_t i = 0; i + 2 < units; i++)if (!in->environment[i] && !in->environment[i + 1])return PP_INVALID;
    size_t env_size = pp_align(eb), need = total + env_size;
    if (pp_overlap(required, sizeof(*required), in, sizeof(*in)) ||
        pp_overlap(required, sizeof(*required), in->environment, eb) ||
        (output && pp_overlap(required, sizeof(*required), output, capacity)))
        return PP_INVALID;
    for (unsigned i = 0; i < 8; i++)if (pp_overlap(required, sizeof(*required), in->strings[i].data,
                                                   in->strings[i].length))return PP_INVALID;
    *required = need;
    if (!output || capacity < need)return PP_SMALL;
    if ((uintptr_t) output & 7)return PP_INVALID;
    if (pp_overlap(output, need, in, sizeof(*in)) || pp_overlap(output, need, in->environment, eb) ||
        pp_overlap(output, need, required, sizeof(*required)))
        return PP_INVALID;
    for (unsigned i = 0; i < 8; i++)if (pp_overlap(output, need, in->strings[i].data, in->strings[i].length))return
            PP_INVALID;
    uint8_t *p = output;
    for (size_t i = 0; i < need; i++)p[i] = 0;
    pp_put32(p, (uint32_t) total);
    pp_put32(p + 4, (uint32_t) total);
    pp_put32(p + 8, in->flags);
    pp_put32(p + 0x18, in->console_flags);
    for (unsigned i = 0; i < 3; i++)pp_put64(p + 0x20 + 8 * i, in->standard_handles[i]);
    const unsigned order[] = {2, 1, 0, 3, 4, 5, 6, 7}, where[] = {0x60, 0x50, 0x38, 0x70, 0xb0, 0xc0, 0xd0, 0xe0};
    size_t cursor = 0x440;
    for (unsigned j = 0; j < 8; j++) {
        unsigned i = order[j];
        if (i == 7 && !caps[i])continue;
        pp_string s = in->strings[i];
        uint8_t *desc = p + where[i];
        pp_put16(desc, (uint16_t)(s.length + (i == 2 ? add_slash * 2 : 0)));
        pp_put16(desc + 2, caps[i]);
        pp_put64(desc + 8, in->flags ? (uint64_t)(uintptr_t)(p + cursor) : cursor);
        for (unsigned k = 0; k < s.length / 2; k++)pp_put16(p + cursor + 2 * k, s.data[k]);
        if (i == 2 && add_slash)pp_put16(p + cursor + s.length, '\\');
        cursor += pp_align(caps[i]);
    }
    pp_put64(p + 0x80, (uint64_t)(uintptr_t)(p + total)); /* Environment stays absolute. */
    pp_put64(p + 0x3f0, env_size);
    for (size_t i = 0; i < eb / 2; i++)pp_put16(p + total + 2 * i, in->environment[i]);
    return 0;
}
#endif
