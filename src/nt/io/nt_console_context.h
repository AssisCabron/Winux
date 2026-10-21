#include "../../console/console_line.h"

typedef struct {
    console_line line;
    u64 waits;
    int announced, diagnostic;
} nt_console_device;

static u32 nt_console_read(void *context, void *buffer, u32 count, u64 *transferred) {
    nt_console_device *device = context;
    u64 flags;
    __asm__ volatile("pushfq; pop %0":"=r"(flags));
    __asm__ volatile("cli":::"memory");
    u32 result;
    while ((result = console_line_read(&device->line, buffer, count, transferred)) == CONSOLE_NEED_INPUT) {
        int c = (inb(0x3fd) & 1) ? inb(0x3f8) : keyboard_char();
        if (c) {
            unsigned action = console_line_feed(&device->line, (unsigned) c);
            if (action == 1)console_char((char) c);
            else if (action == 2)console_char('\b');
            else if (action == 3)console_char('\n');
        } else {
            if (device->diagnostic && !device->announced) {
                print("NT_CONSOLE_WAITING\n");
                device->announced = 1;
            }
            device->waits++;
            __asm__ volatile("sti; hlt; cli":::"memory");
        }
    }
    if (flags & 0x200)__asm__ volatile("sti":::"memory");
    return result;
}

static u32 nt_console_write(void *context, void *buffer, u32 count, u64 *transferred) {
    (void) context;
    for (u32 i = 0; i < count; i++)console_char(((const char *) buffer)[i]);
    *transferred = count;
    return 0;
}

/* Storage belongs to the native process context, never to a caller stack. */
static struct {
    nt_console_device device;
    io_endpoint endpoint;
    u64 handles[3], previous[3];
    int active;
} nt_console;

static void nt_console_stop(void) {
    if (!nt_console.active)return;
    for (unsigned i = 0; i < 3; i++) {
        pp_put64(app_parameters + 0x20 + i * 8, nt_console.previous[i]);
        /* A program may have closed a standard handle. Generation checks prevent
         * closing an unrelated object subsequently allocated in the same slot. */
        if (obj_lookup(&nt_objects, nt_console.handles[i]))obj_close(&nt_objects, nt_console.handles[i]);
    }
    zero_bytes(&nt_console, sizeof(nt_console));
}

static int nt_console_start(void) {
    if (nt_console.active)return 0;
    zero_bytes(&nt_console, sizeof(nt_console));
    nt_console.endpoint = (io_endpoint)
    {
        &nt_console.device, nt_console_read, nt_console_write, 0x50, 0
    };
    for (unsigned i = 0; i < 3; i++) {
        nt_console.previous[i] = rd64(app_parameters + 0x20 + i * 8);
        if (io_endpoint_open(&nt_objects, &nt_io, &nt_console.endpoint, i ? OBJ_FILE_WRITE : OBJ_FILE_READ,
                             &nt_console.handles[i])) {
            for (unsigned j = 0; j < i; j++)obj_close(&nt_objects, nt_console.handles[j]);
            zero_bytes(&nt_console, sizeof(nt_console));
            return 0;
        }
    }
    for (unsigned i = 0; i < 3; i++)pp_put64(app_parameters + 0x20 + i * 8, nt_console.handles[i]);
    nt_console.active = 1;
    return 1;
}
