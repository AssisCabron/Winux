/* Original framebuffer console. RGB logical colors converted for GOP RGB/BGR. */
#include "../../build/font.h"
static boot_context *boot;
static u32 columns, rows, cursor_x, cursor_y;
static u32 cells[64][128];
static int graphics_ready;

enum { CONSOLE_MARGIN = 16, CELL_WIDTH = 12, CELL_HEIGHT = 22 };

static u32 rgb(u32 color) {
    return boot->pixel_format == 0 ? ((color & 255) << 16) | (color & 0xff00) | ((color >> 16) & 255) : color;
}

static void rectangle(u32 x, u32 y, u32 w, u32 h, u32 color) {
    if (!graphics_ready || x >= boot->width || y >= boot->height) return;
    if (w > boot->width - x)w = boot->width - x;
    if (h > boot->height - y)h = boot->height - y;
    volatile u32 *fb = (volatile u32 *) boot->framebuffer;
    color = rgb(color);
    for (u32 yy = y; yy < y + h; yy++)for (u32 xx = x; xx < x + w; xx++)fb[(u64) yy * boot->stride + xx] = color;
}

/* Original accent marks composed over the original ASCII bitmap font.
   Accent codes: acute, grave, circumflex, tilde, diaeresis, cedilla. */
static void glyph(u32 x, u32 y, u32 ch, u32 color) {
    u32 c = ch, accent = 0;
    static const struct {
        u32 scalar;
        u8 base, mark;
    } composed[] = {
        {0xc0, 'A', 2}, {0xc1, 'A', 1}, {0xc2, 'A', 3}, {0xc3, 'A', 4}, {0xc4, 'A', 5},
        {0xc7, 'C', 6}, {0xc8, 'E', 2}, {0xc9, 'E', 1}, {0xca, 'E', 3}, {0xcb, 'E', 5},
        {0xcc, 'I', 2}, {0xcd, 'I', 1}, {0xce, 'I', 3}, {0xcf, 'I', 5}, {0xd1, 'N', 4},
        {0xd2, 'O', 2}, {0xd3, 'O', 1}, {0xd4, 'O', 3}, {0xd5, 'O', 4}, {0xd6, 'O', 5},
        {0xd9, 'U', 2}, {0xda, 'U', 1}, {0xdb, 'U', 3}, {0xdc, 'U', 5}, {0xdd, 'Y', 1},
        {0xe0, 'a', 2}, {0xe1, 'a', 1}, {0xe2, 'a', 3}, {0xe3, 'a', 4}, {0xe4, 'a', 5},
        {0xe7, 'c', 6}, {0xe8, 'e', 2}, {0xe9, 'e', 1}, {0xea, 'e', 3}, {0xeb, 'e', 5},
        {0xec, 'i', 2}, {0xed, 'i', 1}, {0xee, 'i', 3}, {0xef, 'i', 5}, {0xf1, 'n', 4},
        {0xf2, 'o', 2}, {0xf3, 'o', 1}, {0xf4, 'o', 3}, {0xf5, 'o', 4}, {0xf6, 'o', 5},
        {0xf9, 'u', 2}, {0xfa, 'u', 1}, {0xfb, 'u', 3}, {0xfc, 'u', 5}, {0xfd, 'y', 1}, {0xff, 'y', 5}
    };
    for (u32 i = 0; i < sizeof(composed) / sizeof(composed[0]); i++)
        if (ch == composed[i].scalar) {
            c = composed[i].base;
            accent = composed[i].mark;
            break;
        }
    if (c >= 128)c = '?';
    u32 base_y = y + (accent && accent != 6 ? 2 : 0);
    for (u32 yy = 0; yy < 7; yy++)
        for (u32 xx = 0; xx < 5; xx++)
            if (font[c][yy] & (1 << (4 - xx)))rectangle(x + xx * 2, base_y + yy * 2, 2, 2, color);
    if (accent == 1) {
        rectangle(x + 6, y, 2, 1, color);
        rectangle(x + 4, y + 1, 2, 1, color);
    }
    if (accent == 2) {
        rectangle(x + 2, y, 2, 1, color);
        rectangle(x + 4, y + 1, 2, 1, color);
    }
    if (accent == 3) {
        rectangle(x + 4, y, 2, 1, color);
        rectangle(x + 2, y + 1, 2, 1, color);
        rectangle(x + 6, y + 1, 2, 1, color);
    }
    if (accent == 4) {
        rectangle(x + 2, y + 1, 2, 1, color);
        rectangle(x + 4, y, 2, 1, color);
        rectangle(x + 6, y + 1, 2, 1, color);
    }
    if (accent == 5) {
        rectangle(x + 2, y, 2, 1, color);
        rectangle(x + 6, y, 2, 1, color);
    }
    if (accent == 6) {
        rectangle(x + 4, y + 14, 2, 1, color);
        rectangle(x + 2, y + 15, 2, 1, color);
    }
}

static void draw_cell(u32 x, u32 y) {
    rectangle(CONSOLE_MARGIN + x * CELL_WIDTH, CONSOLE_MARGIN + y * CELL_HEIGHT, CELL_WIDTH, CELL_HEIGHT, 0x000000);
    glyph(CONSOLE_MARGIN + x * CELL_WIDTH, CONSOLE_MARGIN + y * CELL_HEIGHT + 2, cells[y][x], 0xeeeeee);
}

static void cursor(int show) {
    rectangle(CONSOLE_MARGIN + cursor_x * CELL_WIDTH, CONSOLE_MARGIN + cursor_y * CELL_HEIGHT + 19, 10, 2,
              show ? 0xeeeeee : 0x000000);
}

static void console_clear(void) {
    for (u32 y = 0; y < 64; y++)for (u32 x = 0; x < 128; x++)cells[y][x] = ' ';
    cursor_x = cursor_y = 0;
    rectangle(0, 0, boot->width, boot->height, 0x000000);
}

static void console_init(boot_context *ctx) {
    boot = ctx;
    graphics_ready = ctx->framebuffer && ctx->pixel_format < 2 && ctx->width >= 320 && ctx->height >= 240 &&
                     ctx->stride >= ctx->width && (u64) ctx->stride * ctx->height * 4 <= ctx->framebuffer_size;
    columns = graphics_ready ? (ctx->width - 2 * CONSOLE_MARGIN) / CELL_WIDTH : 80;
    if (columns > 128)columns = 128;
    rows = graphics_ready ? (ctx->height - 2 * CONSOLE_MARGIN) / CELL_HEIGHT : 25;
    if (rows > 64)rows = 64;
    console_clear();
}

static void console_scroll(void) {
    if (cursor_y >= rows) {
        for (u32 y = 1; y < rows; y++)for (u32 x = 0; x < columns; x++)cells[y - 1][x] = cells[y][x];
        for (u32 x = 0; x < columns; x++)cells[rows - 1][x] = ' ';
        cursor_y = rows - 1;
        for (u32 y = 0; y < rows; y++)for (u32 x = 0; x < columns; x++)draw_cell(x, y);
    }
}

static void console_put_cell(u32 scalar) {
    cells[cursor_y][cursor_x] = scalar;
    draw_cell(cursor_x, cursor_y);
    if (++cursor_x == columns) {
        cursor_x = 0;
        cursor_y++;
    }
    console_scroll();
}

static void console_unicode(u32 scalar) {
    if (scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff))scalar = 0xfffd;
    char text[5] = {0};
    if (scalar < 0x80)text[0] = (char) scalar;
    else if (scalar < 0x800) {
        text[0] = (char) (0xc0 | (scalar >> 6));
        text[1] = (char) (0x80 | (scalar & 63));
    } else if (scalar < 0x10000) {
        text[0] = (char) (0xe0 | (scalar >> 12));
        text[1] = (char) (0x80 | ((scalar >> 6) & 63));
        text[2] = (char) (0x80 | (scalar & 63));
    } else {
        text[0] = (char) (0xf0 | (scalar >> 18));
        text[1] = (char) (0x80 | ((scalar >> 12) & 63));
        text[2] = (char) (0x80 | ((scalar >> 6) & 63));
        text[3] = (char) (0x80 | (scalar & 63));
    }
    /* Preserve the established serial LF => CRLF convention. */
    if (scalar == '\n')serial("\r");
    if (scalar == 0) {
        unsigned spin = 100000;
        while (!(inb(0x3fd) & 32) && --spin) {
        }
        if (spin)outb(0x3f8, 0);
    } else serial(text);
    cursor(0);
    if (scalar == '\r')cursor_x = 0;
    else if (scalar == '\n') {
        cursor_x = 0;
        cursor_y++;
        console_scroll();
    } else if (scalar == '\b') {
        if (cursor_x || cursor_y) {
            if (!cursor_x) {
                --cursor_y;
                cursor_x = columns;
            }
            --cursor_x;
            cells[cursor_y][cursor_x] = ' ';
            draw_cell(cursor_x, cursor_y);
        }
    } else if (scalar == '\t') {
        u32 spaces = 8 - (cursor_x % 8);
        while (spaces--)console_put_cell(' ');
    } else if (scalar >= 32 && scalar != 127)console_put_cell(scalar);
    cursor(1);
}

static void console_char(char ch) { console_unicode((u8) ch); }
static void print(const char *s) { while (*s)console_char(*s++); }

static void number(u64 n) {
    char b[24];
    unsigned i = 0;
    do {
        b[i++] = (char) ('0' + n % 10);
        n /= 10;
    } while (n);
    while (i)console_char(b[--i]);
}

static void print_hex(u64 n) {
    char b[19] = "0x0000000000000000";
    for (int i = 17; i >= 2; i--) {
        b[i] = "0123456789abcdef"[n & 15];
        n >>= 4;
    }
    print(b);
}
