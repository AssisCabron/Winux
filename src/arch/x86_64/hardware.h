/* One CPU, legacy PC PIC/PIT. Own basic GDT; paging is initialized separately after the IDT. */
volatile u64 timer_ticks;
static u64 idle_wakes;

typedef struct __attribute__((packed)) {
    u16 low, selector;
    u8 ist, flags;
    u16 middle;
    u32 high, zero;
} idt_gate;

typedef struct __attribute__((packed)) {
    u16 limit;
    u64 base;
} idt_pointer;

static idt_gate idt[256] __attribute__((aligned(16)));
#ifdef WINUX_RING3_PREVIEW
static u64 kernel_gdt[7] __attribute__((aligned(16))) = {0, 0x00af9a000000ffffULL, 0x00cf92000000ffffULL};
#else
static u64 kernel_gdt[3] __attribute__((aligned(16))) = {0, 0x00af9a000000ffffULL, 0x00cf92000000ffffULL};
#endif

__attribute__((naked)) static void reload_segments(void) {
    __asm__ volatile(
        "pushq $8; leaq 1f(%rip),%rax; pushq %rax; lretq; 1: movw $16,%ax; movw %ax,%ds; movw %ax,%es; movw %ax,%ss; movw %ax,%fs; movw %ax,%gs; ret"
    );
}

__attribute__((noreturn)) void kernel_fault(u64 vector, u64 error) {
    __asm__ volatile("cli");
    serial("KERNEL: FAULT vector=");
    hex(vector);
    serial("error=");
    hex(error);
    if (vector == 14) {
        u64 cr2;
        __asm__ volatile("mov %%cr2,%0":"=r"(cr2));
        serial("CR2=");
        hex(cr2);
    }
    if (boot) {
        print("\nKERNEL FAULT ");
        number(vector);
        print(" ERROR ");
        print_hex(error);
        print("\nCPU halted. See serial log.\n");
    }
    for (;;)__asm__ volatile("hlt");
}

#include "../../../build/exceptions.h"

__attribute__((naked)) static void timer_interrupt(void) {
    __asm__ volatile("pushq %rax; incq timer_ticks(%rip); movb $0x20,%al; outb %al,$0x20; popq %rax; iretq");
}

__attribute__((naked)) static void unused_interrupt(void) { __asm__ volatile("iretq"); }
#ifdef WINUX_ORBIS_GUI_PREVIEW
/* IRQ12 AUX input buffer. Never paint the framebuffer inside the ISR.
 * One CPU / bounded 128 byte ring; the UI drains outside IRQ context. */
static volatile u8 or_ps2_ring[128];
static volatile u32 or_ps2_rd,or_ps2_wr,or_ps2_irq_count;
__attribute__((naked)) static void or_ps2_irq12(void){
    __asm__ volatile(
        "pushq %%rax\n\tpushq %%rcx\n\tpushq %%rdx\n\t"
        "movw $0x64, %%dx\n\tinb %%dx, %%al\n\t"
        "andb $0x21, %%al\n\tcmpb $0x21, %%al\n\tjne 1f\n\t"
        "movw $0x60, %%dx\n\tinb %%dx, %%al\n\t"
        "movl or_ps2_wr(%%rip), %%ecx\n\t"
        "leal 1(%%rcx), %%edx\n\tandl $127, %%edx\n\t"
        "cmpl or_ps2_rd(%%rip), %%edx\n\tje 1f\n\t"
        "leaq or_ps2_ring(%%rip), %%rdx\n\t"
        "movb %%al,(%%rdx,%%rcx,1)\n\t"
        "incl %%ecx\n\tandl $127, %%ecx\n\t"
        "movl %%ecx,or_ps2_wr(%%rip)\n\t"
        "1: movb $0x20, %%al\n\t"
        "outb %%al,$0xa0\n\toutb %%al,$0x20\n\t"
        "popq %%rdx\n\tpopq %%rcx\n\tpopq %%rax\n\tiretq"
        :::"memory");
}
#endif


static void set_gate(unsigned i, void (*fn)(void), u16 cs) {
    u64 a = (u64) fn;
    idt[i] = (idt_gate){(u16) a, cs, 0, 0x8e, (u16)(a >> 16), (u32)(a >> 32), 0};
}

static void io_wait(void) { outb(0x80, 0); }

static void hardware_init(void) {
    idt_pointer gdtr = {sizeof(kernel_gdt) - 1, (u64) kernel_gdt};
    __asm__ volatile("lgdt %0"::"m"(gdtr):"memory");
    reload_segments();
    /* Own the FP/SSE state rather than inheriting firmware or hypervisor defaults. */
    u64 cr0, cr4;
    __asm__ volatile("mov %%cr0,%0; mov %%cr4,%1":"=r"(cr0),"=r"(cr4));
    cr0 = (cr0 & ~12ULL) | 0x22;
    cr4 |= 0x600;
    __asm__ volatile("mov %0,%%cr0; mov %1,%%cr4"::"r"(cr0),"r"(cr4):"memory");
    u32 initial_mxcsr = 0x1f80;
    /* Exercise x87 once and leave its stack empty before the first context save. */
    __asm__ volatile("fninit; fld1; fstp %%st(0); ldmxcsr %0"::"m"(initial_mxcsr):"memory");
    u16 cs;
    __asm__ volatile("mov %%cs,%0":"=r"(cs));
    for (unsigned i = 0; i < 256; i++)set_gate(i, unused_interrupt, cs);
    for (unsigned i = 0; i < 32; i++)set_gate(i, exception_handlers[i], cs);
    set_gate(32, timer_interrupt, cs);
#ifdef WINUX_ORBIS_GUI_PREVIEW
    set_gate(44, or_ps2_irq12, cs);
#endif
    idt_pointer pointer = {sizeof(idt) - 1, (u64) idt};
    __asm__ volatile("lidt %0"::"m"(pointer):"memory");
    outb(0x21, 255);
    outb(0xa1, 255);
    outb(0x20, 0x11);
    io_wait();
    outb(0xa0, 0x11);
    io_wait();
    outb(0x21, 0x20);
    io_wait();
    outb(0xa1, 0x28);
    io_wait();
    outb(0x21, 4);
    io_wait();
    outb(0xa1, 2);
    io_wait();
    outb(0x21, 1);
    io_wait();
    outb(0xa1, 1);
    io_wait();
    outb(0x21, 0xfe);
    outb(0xa1, 255);
    outb(0x43, 0x36);
    outb(0x40, 11932 & 255);
    outb(0x40, 11932 >> 8);
    __asm__ volatile("xor %%eax,%%eax; mov %%rax,%%cr8":::"rax","memory");
}

static int ps2_wait(void) {
    for (unsigned i = 0; i < 100000; i++) {
        if (!(inb(0x64) & 2))return 1;
        __asm__ volatile("pause");
    }
    return 0;
}

static void keyboard_init(void) {
    /* Bounded setup; enable first port, force translated set-1, poll with IRQ1 masked. */
    for (unsigned i = 0; i < 64 && (inb(0x64) & 1); i++)(void) inb(0x60);
    if (!ps2_wait())return;
    outb(0x64, 0xae);
    if (!ps2_wait())return;
    outb(0x64, 0x20);
    u8 cfg = 0x40;
    int got = 0;
    for (unsigned i = 0; i < 100000; i++)
        if (inb(0x64) & 1) {
            cfg = inb(0x60);
            got = 1;
            break;
        }
    if (!got)return;
    cfg = (cfg | 0x40) & ~0x11;
    if (!ps2_wait())return;
    outb(0x64, 0x60);
    if (!ps2_wait())return;
    outb(0x60, cfg);
    if (!ps2_wait())return;
    outb(0x60, 0xf4);
}

static int keyboard_char(void) {
    static int shift, extended;
    static const char normal[128] = {
        [2] = '1', [3] = '2', [4] = '3', [5] = '4', [6] = '5', [7] = '6', [8] = '7', [9] = '8', [10] = '9', [11] = '0',
        [12] = '-', [13] = '=', [14] = '\b', [16] = 'q', [17] = 'w', [18] = 'e', [19] = 'r', [20] = 't', [21] = 'y',
        [22] = 'u', [23] = 'i', [24] = 'o', [25] = 'p', [26] = '[', [27] = ']', [28] = '\n', [30] = 'a', [31] = 's',
        [32] = 'd', [33] = 'f', [34] = 'g', [35] = 'h', [36] = 'j', [37] = 'k', [38] = 'l', [39] = ';', [40] = '\'',
        [41] = '`', [43] = '\\', [44] = 'z', [45] = 'x', [46] = 'c', [47] = 'v', [48] = 'b', [49] = 'n', [50] = 'm',
        [51] = ',', [52] = '.', [53] = '/', [57] = ' '
    };
    u8 status = inb(0x64);
    if (!(status & 1))return 0;
    u8 code = inb(0x60);
    if (status & 0xe0)return 0;
    if (code == 0xe0 || code == 0xe1) {
        extended = 1;
        return 0;
    }
    if (extended) {
        extended = 0;
        return 0;
    }
    if (code == 42) {
        shift |= 1;
        return 0;
    }
    if (code == 54) {
        shift |= 2;
        return 0;
    }
    if (code == 170) {
        shift &= ~1;
        return 0;
    }
    if (code == 182) {
        shift &= ~2;
        return 0;
    }
    if (code >= 128)return 0;
    char c = normal[code];
    if (shift) {
        if (c >= 'a' && c <= 'z')c -= 32;
        else {
            const char *a = "1234567890-=[];',./", *b = "!@#$%^&*()_+{}:\"<>?";
            for (unsigned i = 0; a[i]; i++)
                if (c == a[i]) {
                    c = b[i];
                    break;
                }
        }
    }
    return c;
}
