/* ASCII line buffering; hardware waiting and echo belong to the caller.
 * NEED_INPUT is internal and must never escape as an NT completion status. */
#ifndef WINUX_CONSOLE_LINE_H
#define WINUX_CONSOLE_LINE_H
#include <stdint.h>
#define CONSOLE_NEED_INPUT UINT32_C(0xffffffff)

typedef struct {
    uint8_t bytes[256];
    unsigned length, position, ready, skip_lf;
} console_line;

/* 0 ignored, 1 appended, 2 erased, 3 completed. Do not poll input while ready. */
static unsigned console_line_feed(console_line *line, unsigned ch) {
    if (line->ready)return 0;
    if (ch == '\n' && line->skip_lf) {
        line->skip_lf = 0;
        return 0;
    }
    line->skip_lf = 0;
    if (ch == '\r' || ch == '\n') {
        line->bytes[line->length++] = '\r';
        line->bytes[line->length++] = '\n';
        line->ready = 1;
        line->skip_lf = ch == '\r';
        return 3;
    }
    if (ch == 8 || ch == 127) {
        if (line->length) {
            line->length--;
            return 2;
        }
        return 0;
    }
    if (ch >= 32 && ch < 127 && line->length < 254) {
        line->bytes[line->length++] = (uint8_t) ch;
        return 1;
    }
    return 0;
}

static uint32_t console_line_read(console_line *line, void *buffer, uint32_t count, uint64_t *transferred) {
    if (!count) {
        *transferred = 0;
        return 0;
    }
    if (!line->ready)return CONSOLE_NEED_INPUT;
    unsigned amount = line->length - line->position;
    if (amount > count)amount = count;
    for (unsigned i = 0; i < amount; i++)((uint8_t *) buffer)[i] = line->bytes[line->position + i];
    line->position += amount;
    *transferred = amount;
    if (line->position == line->length) {
        line->length = 0;
        line->position = 0;
        line->ready = 0;
    }
    return 0;
}
#endif
