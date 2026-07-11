#include "idt.h"
#include "types.h"
#include "io.h"

extern void timer_isr_wrapper(void);
extern void keyboard_isr_wrapper(void);

struct idt_entry {
    uint16_t offset_low; uint16_t selector; uint8_t ist;
    uint8_t flags; uint16_t offset_mid; uint32_t offset_high; uint32_t zero;
} __attribute__((packed));

struct idtr { uint16_t limit; uint64_t base; } __attribute__((packed));

struct idt_entry idt[256];
struct idtr idtp;

void set_idt_gate(int n, uint64_t handler) {
    idt[n].offset_low = (uint16_t)(handler & 0xFFFF);
    idt[n].selector = 0x18;
    idt[n].ist = 0;
    idt[n].flags = 0x8E;
    idt[n].offset_mid = (uint16_t)((handler >> 16) & 0xFFFF);
    idt[n].offset_high = (uint32_t)((handler >> 32) & 0xFFFFFFFF);
    idt[n].zero = 0;
}

void init_idt() {
    idtp.limit = (sizeof(struct idt_entry) * 256) - 1;
    idtp.base = (uint64_t)&idt;

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

    __asm__ volatile ("lidt %0" : : "m"(idtp));
    __asm__ volatile ("sti");
}