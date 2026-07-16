#include "idt.h"
#include "types.h"
#include "io.h"
#include "vga.h"
#include "event.h"
#include "serial.h"
#include "user.h"

extern void timer_isr_wrapper(void);
extern void keyboard_isr_wrapper(void);
extern void syscall_isr_wrapper(void);
extern void *exception_stub_table[];

#define CPU_EXCEPTION_COUNT 32

struct idt_entry {
    uint16_t offset_low; uint16_t selector; uint8_t ist;
    uint8_t flags; uint16_t offset_mid; uint32_t offset_high; uint32_t zero;
} __attribute__((packed));

struct idtr { uint16_t limit; uint64_t base; } __attribute__((packed));
struct gdtr { uint16_t limit; uint64_t base; } __attribute__((packed));
struct tss64 {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed));

struct idt_entry idt[256];
struct idtr idtp;
static uint64_t gdt[7];
static struct gdtr gdtp;
static struct tss64 tss;
static uint8_t kernel_interrupt_stack[4096] __attribute__((aligned(16)));

static const char *const exception_names[CPU_EXCEPTION_COUNT] = {
    "Divide error", "Debug", "Non-maskable interrupt", "Breakpoint",
    "Overflow", "Bound range exceeded", "Invalid opcode", "Device not available",
    "Double fault", "Coprocessor segment overrun", "Invalid TSS", "Segment not present",
    "Stack-segment fault", "General protection fault", "Page fault", "Reserved",
    "x87 floating-point exception", "Alignment check", "Machine check", "SIMD floating-point exception",
    "Virtualization exception", "Control protection exception", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved", "Hypervisor injection exception",
    "VMM communication exception", "Security exception", "Reserved"
};

static void set_idt_gate_flags(int n, uint64_t handler, uint8_t flags) {
    idt[n].offset_low = (uint16_t)(handler & 0xFFFF);
    idt[n].selector = 0x08;
    idt[n].ist = 0;
    idt[n].flags = flags;
    idt[n].offset_mid = (uint16_t)((handler >> 16) & 0xFFFF);
    idt[n].offset_high = (uint32_t)((handler >> 32) & 0xFFFFFFFF);
    idt[n].zero = 0;
}

static void set_idt_gate(int n, uint64_t handler) {
    set_idt_gate_flags(n, handler, 0x8E);
}

static void init_gdt_tss(void) {
    uint64_t tss_base = (uint64_t)&tss;
    uint32_t tss_limit = sizeof(tss) - 1;
    gdt[0] = 0;
    gdt[1] = 0x00209A0000000000ULL;
    gdt[2] = 0x0000920000000000ULL;
    gdt[3] = 0x0000F20000000000ULL;
    gdt[4] = 0x0020FA0000000000ULL;
    gdt[5] = ((uint64_t)tss_limit & 0xFFFFULL) |
             ((tss_base & 0xFFFFFFULL) << 16) |
             (0x89ULL << 40) |
             (((uint64_t)tss_limit & 0xF0000ULL) << 32) |
             (((uint64_t)tss_base >> 24) & 0xFFULL) << 56;
    gdt[6] = tss_base >> 32;
    tss.rsp0 = (uint64_t)(kernel_interrupt_stack + sizeof(kernel_interrupt_stack));
    tss.iomap_base = sizeof(tss);
    gdtp.limit = sizeof(gdt) - 1;
    gdtp.base = (uint64_t)&gdt;
    __asm__ volatile ("lgdt %0" : : "m"(gdtp));
    __asm__ volatile ("mov $0x10, %%ax; mov %%ax, %%ds; mov %%ax, %%es; mov %%ax, %%ss" ::: "rax");
    __asm__ volatile ("ltr %%ax" : : "a"((uint16_t)0x28));
    serial_write("GlassOS: GDT/TSS ready\r\n");
}

int exception_handler(uint64_t vector, uint64_t error_code, void *frame) {
    __asm__ volatile ("cli");
    if (user_handle_fault(vector, error_code, frame)) {
        __asm__ volatile ("sti");
        return 1;
    }
    event_record("cpu", "kernel", "exception", EVENT_FAULT, 0, "deny:fault");
    serial_write("EXCEPTION vector="); serial_write_decimal(vector); serial_write(" error="); serial_write_hex(error_code); serial_write("\r\n");
    vga_set_color(VGA_COLOR_LIGHT_RED_ON_BLACK);
    clear_screen();
    printf("CPU EXCEPTION\n%s\nvector: %d  error: %x\nSystem halted.\n",
           exception_names[vector], (int)vector, error_code);

    while (1) {
        __asm__ volatile ("hlt");
    }
    return 0;
}

void init_idt(void) {
    serial_write("GlassOS: building IDT\r\n");
    init_gdt_tss();
    idtp.limit = (sizeof(struct idt_entry) * 256) - 1;
    idtp.base = (uint64_t)&idt;

    for (int vector = 0; vector < CPU_EXCEPTION_COUNT; vector++) {
        set_idt_gate(vector, (uint64_t)exception_stub_table[vector]);
    }
    serial_write("GlassOS: IDT gates ready\r\n");

    outb(0x20, 0x11); outb(0xA0, 0x11);
    outb(0x21, 0x20); outb(0xA1, 0x28);
    outb(0x21, 0x04); outb(0xA1, 0x02);
    outb(0x21, 0x01); outb(0xA1, 0x01);

    outb(0x43, 0x36);
    uint16_t divisor = 11931;
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));

    outb(0x21, 0xFC);
    outb(0xA1, 0xFF);

    set_idt_gate(32, (uint64_t)timer_isr_wrapper);
    set_idt_gate(33, (uint64_t)keyboard_isr_wrapper);
    set_idt_gate_flags(0x80, (uint64_t)syscall_isr_wrapper, 0xEE);
    serial_write("GlassOS: PIC ready\r\n");

    __asm__ volatile ("lidt %0" : : "m"(idtp));
    serial_write("GlassOS: IDT loaded\r\n");
    __asm__ volatile ("sti");
}
