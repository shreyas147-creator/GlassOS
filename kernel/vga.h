#ifndef VGA_H
#define VGA_H

void vga_enable_cursor(void);
void vga_update_cursor(void);
void vga_move_cursor_left(void);
void vga_move_cursor_right(void);
void vga_backspace(void);
void clear_screen(void);
void print_char(char c);
void printf(const char *format, ...);

#endif
