#include "types.h"
#include "vga.h"
#include "idt.h"
#include "memory.h"
#include "timer.h"
#include "keyboard.h"
#include "io.h"

// Provided by linker.ld - bounds of the .bss section
extern uint8_t __bss_start[];
extern uint8_t __bss_end[];

int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

// objcopy -O binary does not write zero bytes for .bss - it only tracks
// the size. Since stage2 copies the raw file bytes from disk into RAM,
// every .bss variable (kbd_buffer, buffer_idx, command_ready, idt[256],
// system_ticks, ...) starts as whatever was already sitting in that RAM,
// not zero. Zero it explicitly before anything else runs.
void zero_bss(void) {
    for (uint8_t *p = __bss_start; p < __bss_end; p++) {
        *p = 0;
    }
}

void kernel_main(void) {
    clear_screen();
    vga_enable_cursor();

    while (inb(0x64) & 1) {
        inb(0x60);
    }

    init_idt();

    printf("Glass64 Shell v1.1 - Modular Architecture\n");

    while (1) {
        __asm__ volatile ("cli");
        keyboard_begin_line();
        printf("> ");
        __asm__ volatile ("sti");

        while (!command_ready) {
            __asm__ volatile ("hlt");
        }

        if (strcmp(kbd_buffer, "help") == 0) {
            printf("Commands: help, clear, meminfo, time\n");
        }
        else if (strcmp(kbd_buffer, "clear") == 0) {
            clear_screen();
        }
        else if (strcmp(kbd_buffer, "meminfo") == 0) {
            printf("Heap ptr: %x\n", heap_ptr);
        }
        else if (strcmp(kbd_buffer, "time") == 0) {
            unsigned int seconds = (unsigned int)(system_ticks / 100);
            printf("System active for: %d seconds (%x ticks)\n", seconds, system_ticks);
        }
        else {
            printf("Unknown command: %s\n", kbd_buffer);
        }

    }
}
