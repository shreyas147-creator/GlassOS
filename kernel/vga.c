#include "vga.h"
#include "types.h"
#include "io.h"
#include "serial.h"

#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define QEMU_DEBUG_PORT 0xE9
#define WINDOW_LEFT 0
#define WINDOW_RIGHT (VGA_WIDTH - 1)
#define WINDOW_TOP 0
#define WINDOW_BOTTOM (VGA_HEIGHT - 1)
#define CONSOLE_LEFT 2
#define CONSOLE_RIGHT 77
#define CONSOLE_TOP 3
#define CONSOLE_BOTTOM 21

volatile uint8_t *video_memory = (volatile uint8_t *)0xB8000;
int cursor_x = 0;
int cursor_y = 0;
uint8_t current_color = 0x0F;
static int window_mode = 0;

void vga_set_color(uint8_t color) {
    current_color = color;
}

static void write_cell(int x, int y, char c, uint8_t color) {
    int offset = (y * VGA_WIDTH + x) * 2;
    video_memory[offset] = (uint8_t)c;
    video_memory[offset + 1] = color;
}

static void write_text(int x, int y, const char *text) {
    while (*text != '\0' && x < WINDOW_RIGHT) {
        write_cell(x, y, *text, current_color);
        x++;
        text++;
    }
}

static void draw_horizontal_rule(int y) {
    write_cell(WINDOW_LEFT, y, '+', current_color);
    for (int x = WINDOW_LEFT + 1; x < WINDOW_RIGHT; x++) {
        write_cell(x, y, '-', current_color);
    }
    write_cell(WINDOW_RIGHT, y, '+', current_color);
}

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

static void scroll_window_content(void) {
    for (int y = CONSOLE_TOP + 1; y <= CONSOLE_BOTTOM; y++) {
        for (int x = WINDOW_LEFT + 1; x < WINDOW_RIGHT; x++) {
            int target = ((y - 1) * VGA_WIDTH + x) * 2;
            int source = (y * VGA_WIDTH + x) * 2;
            video_memory[target] = video_memory[source];
            video_memory[target + 1] = video_memory[source + 1];
        }
    }

    for (int x = WINDOW_LEFT + 1; x < WINDOW_RIGHT; x++) {
        write_cell(x, CONSOLE_BOTTOM, ' ', current_color);
    }
    cursor_y = CONSOLE_BOTTOM;
    cursor_x = CONSOLE_LEFT;
}

static void keep_cursor_in_console(void) {
    if (window_mode == 0) {
        if (cursor_y >= VGA_HEIGHT) {
            scroll_screen();
        }
        return;
    }

    if (cursor_x < CONSOLE_LEFT) {
        cursor_x = CONSOLE_LEFT;
    }
    if (cursor_x > CONSOLE_RIGHT) {
        cursor_x = CONSOLE_LEFT;
        cursor_y++;
    }
    if (cursor_y < CONSOLE_TOP) {
        cursor_y = CONSOLE_TOP;
    }
    if (cursor_y > CONSOLE_BOTTOM) {
        scroll_window_content();
    }
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
    if (window_mode != 0) {
        if (cursor_x > CONSOLE_LEFT) {
            cursor_x--;
        } else if (cursor_y > CONSOLE_TOP) {
            cursor_y--;
            cursor_x = CONSOLE_RIGHT;
        }
        vga_update_cursor();
        return;
    }

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
    if (window_mode != 0) {
        keep_cursor_in_console();
        vga_update_cursor();
        return;
    }

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
    write_cell(cursor_x, cursor_y, ' ', current_color);
}

void print_char(char c) {
    /* QEMU's debug console mirrors the shell for automated boot tests. */
    outb(QEMU_DEBUG_PORT, (uint8_t)c);
    serial_write_char(c);

    if (c == '\n') {
        cursor_x = window_mode != 0 ? CONSOLE_LEFT : 0;
        cursor_y++;
    } else {
        keep_cursor_in_console();
        write_cell(cursor_x, cursor_y, c, current_color);
        cursor_x++;

        if (window_mode != 0 && cursor_x > CONSOLE_RIGHT) {
            cursor_x = CONSOLE_LEFT;
            cursor_y++;
        } else if (cursor_x >= VGA_WIDTH) {
            cursor_x = 0;
            cursor_y++;
        }
    }

    keep_cursor_in_console();
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
    window_mode = 0;
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        video_memory[i * 2] = ' '; video_memory[i * 2 + 1] = current_color;
    }
    cursor_x = 0; cursor_y = 0;
    vga_update_cursor();
}

void vga_clear_window_content(void) {
    if (window_mode == 0) {
        clear_screen();
        return;
    }

    for (int y = CONSOLE_TOP; y <= CONSOLE_BOTTOM; y++) {
        for (int x = WINDOW_LEFT + 1; x < WINDOW_RIGHT; x++) {
            write_cell(x, y, ' ', current_color);
        }
    }
    cursor_x = CONSOLE_LEFT;
    cursor_y = CONSOLE_TOP;
    vga_update_cursor();
}

void vga_draw_window(void) {
    vga_set_color(VGA_COLOR_WHITE_ON_BLACK);
    clear_screen();

    draw_horizontal_rule(WINDOW_TOP);
    write_cell(WINDOW_LEFT, 1, '|', current_color);
    write_cell(WINDOW_RIGHT, 1, '|', current_color);
    write_text(2, 1, "GlassOS Control Window");
    write_text(48, 1, "black background / white text");
    draw_horizontal_rule(2);

    for (int y = CONSOLE_TOP; y <= CONSOLE_BOTTOM; y++) {
        write_cell(WINDOW_LEFT, y, '|', current_color);
        write_cell(WINDOW_RIGHT, y, '|', current_color);
    }

    draw_horizontal_rule(22);
    write_cell(WINDOW_LEFT, 23, '|', current_color);
    write_cell(WINDOW_RIGHT, 23, '|', current_color);
    write_text(2, 23, "Type help for commands | clear clears pane | window redraws");
    draw_horizontal_rule(WINDOW_BOTTOM);

    cursor_x = CONSOLE_LEFT;
    cursor_y = CONSOLE_TOP;
    window_mode = 1;
    vga_update_cursor();
}
