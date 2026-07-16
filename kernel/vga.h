#ifndef VGA_H
#define VGA_H

#include "types.h"

#define VGA_COLOR_WHITE_ON_BLACK 0x0F
#define VGA_COLOR_LIGHT_RED_ON_BLACK 0x0C

void vga_enable_cursor(void);
void vga_update_cursor(void);
void vga_move_cursor_left(void);
void vga_move_cursor_right(void);
void vga_backspace(void);
void vga_set_color(uint8_t color);
void vga_draw_window(void);
void vga_clear_window_content(void);
void clear_screen(void);
void print_char(char c);
void printf(const char *format, ...);

#endif
