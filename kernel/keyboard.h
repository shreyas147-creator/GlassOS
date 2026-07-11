#ifndef KEYBOARD_H
#define KEYBOARD_H

extern char kbd_buffer[128];
extern volatile int buffer_idx;
extern volatile int command_ready;
void keyboard_begin_line(void);
void keyboard_handler(void);

#endif
