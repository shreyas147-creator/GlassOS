#include "vga.h"
#include "types.h"
#include "io.h"

#define VGA_WIDTH 80
#define VGA_HEIGHT 25

volatile uint8_t *video_memory = (volatile uint8_t *)0xB8000;
int cursor_x = 0;
int cursor_y = 0;
uint8_t current_color = 0x0F;

static void scroll_screen(void) {
    for (int y = 1; y < VGA_HEIGHT; y++) {
        for (int x = 0; x < VGA_WIDTH; x++) {
            int target = ((y - 1) * VGA_WIDTH + x) * 2;
            int source = (y * VGA_WIDTH + x) * 2;
            video_memory[target] = video_memory[source];
            video_memory[target + 1] = video_memory[source + 1];
        }
    }

    for (int x = 0; x < VGA_WIDTH; x++) {
        int offset = ((VGA_HEIGHT - 1) * VGA_WIDTH + x) * 2;
        video_memory[offset] = ' ';
        video_memory[offset + 1] = current_color;
    }
    cursor_y = VGA_HEIGHT - 1;
}

void vga_enable_cursor(void) {
    outb(0x3D4, 0x0A);
    outb(0x3D5, 14);
    outb(0x3D4, 0x0B);
    outb(0x3D5, 15);
    vga_update_cursor();
}

void vga_update_cursor(void) {
    uint16_t position = (uint16_t)(cursor_y * VGA_WIDTH + cursor_x);
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(position & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)(position >> 8));
}

void vga_move_cursor_left(void) {
    if (cursor_x > 0) {
        cursor_x--;
    } else if (cursor_y > 0) {
        cursor_y--;
        cursor_x = VGA_WIDTH - 1;
    }
    vga_update_cursor();
}

void vga_move_cursor_right(void) {
    cursor_x++;
    if (cursor_x >= VGA_WIDTH) {
        cursor_x = 0;
        cursor_y++;
    }
    if (cursor_y >= VGA_HEIGHT) {
        scroll_screen();
    }
    vga_update_cursor();
}

void vga_backspace(void) {
    if (cursor_x == 0 && cursor_y == 0) {
        return;
    }

    vga_move_cursor_left();
    int offset = (cursor_y * VGA_WIDTH + cursor_x) * 2;
    video_memory[offset] = ' ';
    video_memory[offset + 1] = current_color;
}

void print_char(char c) {
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else {
        int offset = (cursor_y * VGA_WIDTH + cursor_x) * 2;
        video_memory[offset] = c;
        video_memory[offset + 1] = current_color;
        cursor_x++;
        if (cursor_x >= VGA_WIDTH) {
            cursor_x = 0;
            cursor_y++;
        }
    }

    if (cursor_y >= VGA_HEIGHT) {
        scroll_screen();
    }
    vga_update_cursor();
}

void print_string_internal(const char* s) { while(*s) print_char(*s++); }
void print_int_internal(int num) {
    if (num == 0) { print_char('0'); return; }
    if (num < 0) { print_char('-'); num = -num; }
    char buffer[12]; int i = 0;
    while (num > 0) { buffer[i++] = (num % 10) + '0'; num /= 10; }
    while (--i >= 0) print_char(buffer[i]);
}
void print_hex_internal(uint64_t num) {
    print_string_internal("0x");
    if (num == 0) { print_char('0'); return; }
    char buffer[16]; int i = 0;
    while (num > 0) {
        int rem = num % 16;
        buffer[i++] = (rem < 10) ? (rem + '0') : (rem - 10 + 'A');
        num /= 16;
    }
    while (--i >= 0) print_char(buffer[i]);
}

void printf(const char *format, ...) {
    __builtin_va_list args; __builtin_va_start(args, format);
    for (int i = 0; format[i] != '\0'; i++) {
        if (format[i] == '%' && format[i+1] != '\0') {
            i++;
            if (format[i] == 's') print_string_internal(__builtin_va_arg(args, char*));
            else if (format[i] == 'd') print_int_internal(__builtin_va_arg(args, int));
            else if (format[i] == 'x') print_hex_internal(__builtin_va_arg(args, uint64_t));
            else { print_char('%'); print_char(format[i]); }
        } else { print_char(format[i]); }
    }
    __builtin_va_end(args);
}

void clear_screen(void) {
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        video_memory[i * 2] = ' '; video_memory[i * 2 + 1] = current_color;
    }
    cursor_x = 0; cursor_y = 0;
    vga_update_cursor();
}
