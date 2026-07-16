#include "panic.h"
#include "io.h"
#include "vga.h"
#include "serial.h"

void panic_impl(const char *message, const char *file, int line) {
    __asm__ volatile ("cli");
    serial_write("KERNEL PANIC: "); serial_write(message); serial_write("\r\n");
    vga_set_color(VGA_COLOR_LIGHT_RED_ON_BLACK);
    printf("\nKERNEL PANIC\n%s\n%s:%d\n", message, file, line);

    while (1) {
        __asm__ volatile ("hlt");
    }
}
