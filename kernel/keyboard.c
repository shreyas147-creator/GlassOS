#include "keyboard.h"
#include "io.h"
#include "vga.h"

#define LINE_CAPACITY 128
#define HISTORY_CAPACITY 8

char kbd_buffer[128];
volatile int buffer_idx = 0;
volatile int command_ready = 0;

static char command_history[HISTORY_CAPACITY][LINE_CAPACITY];
static char saved_line[LINE_CAPACITY];
static int history_count = 0;
static int history_next = 0;
static int history_cursor = -1;
static int edit_cursor = 0;
static int rendered_length = 0;
static uint8_t extended_scancode = 0;
static uint8_t shift_pressed = 0;

const char kbd_us[128] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' ', 0
};

static char shifted_key(char key) {
    if (key >= 'a' && key <= 'z') {
        return (char)(key - 'a' + 'A');
    }
    switch (key) {
        case '1': return '!';
        case '2': return '@';
        case '3': return '#';
        case '4': return '$';
        case '5': return '%';
        case '6': return '^';
        case '7': return '&';
        case '8': return '*';
        case '9': return '(';
        case '0': return ')';
        case '-': return '_';
        case '=': return '+';
        case '[': return '{';
        case ']': return '}';
        case '\\': return '|';
        case ';': return ':';
        case '\'': return '"';
        case '`': return '~';
        case ',': return '<';
        case '.': return '>';
        case '/': return '?';
        default: return key;
    }
}

static void copy_line(char *destination, const char *source) {
    int i = 0;
    while (i < LINE_CAPACITY - 1 && source[i] != '\0') {
        destination[i] = source[i];
        i++;
    }
    destination[i] = '\0';
}

static int line_length(const char *line) {
    int length = 0;
    while (length < LINE_CAPACITY - 1 && line[length] != '\0') {
        length++;
    }
    return length;
}

static int history_slot(int index) {
    return (history_next - history_count + index + HISTORY_CAPACITY) % HISTORY_CAPACITY;
}

static void redraw_line(void) {
    for (int i = edit_cursor; i < rendered_length; i++) {
        vga_move_cursor_right();
    }
    for (int i = 0; i < rendered_length; i++) {
        vga_backspace();
    }

    for (int i = 0; i < buffer_idx; i++) {
        print_char(kbd_buffer[i]);
    }
    rendered_length = buffer_idx;

    for (int i = edit_cursor; i < buffer_idx; i++) {
        vga_move_cursor_left();
    }
}

static void reset_history_navigation(void) {
    history_cursor = -1;
}

static void replace_line(const char *line) {
    copy_line(kbd_buffer, line);
    buffer_idx = line_length(kbd_buffer);
    edit_cursor = buffer_idx;
    redraw_line();
}

static void remember_command(void) {
    if (buffer_idx == 0) {
        return;
    }

    copy_line(command_history[history_next], kbd_buffer);
    history_next = (history_next + 1) % HISTORY_CAPACITY;
    if (history_count < HISTORY_CAPACITY) {
        history_count++;
    }
}

static void history_up(void) {
    if (history_count == 0) {
        return;
    }
    if (history_cursor < 0) {
        copy_line(saved_line, kbd_buffer);
        history_cursor = history_count - 1;
    } else if (history_cursor > 0) {
        history_cursor--;
    }
    replace_line(command_history[history_slot(history_cursor)]);
}

static void history_down(void) {
    if (history_cursor < 0) {
        return;
    }
    if (history_cursor < history_count - 1) {
        history_cursor++;
        replace_line(command_history[history_slot(history_cursor)]);
    } else {
        history_cursor = -1;
        replace_line(saved_line);
    }
}

static void insert_character(char key) {
    if (buffer_idx >= LINE_CAPACITY - 1) {
        return;
    }
    for (int i = buffer_idx; i > edit_cursor; i--) {
        kbd_buffer[i] = kbd_buffer[i - 1];
    }
    kbd_buffer[edit_cursor++] = key;
    buffer_idx++;
    kbd_buffer[buffer_idx] = '\0';
    reset_history_navigation();
    redraw_line();
}

static void delete_character(void) {
    if (edit_cursor >= buffer_idx) {
        return;
    }
    for (int i = edit_cursor; i < buffer_idx - 1; i++) {
        kbd_buffer[i] = kbd_buffer[i + 1];
    }
    kbd_buffer[--buffer_idx] = '\0';
    reset_history_navigation();
    redraw_line();
}

static void backspace_character(void) {
    if (edit_cursor == 0) {
        return;
    }
    edit_cursor--;
    vga_move_cursor_left();
    delete_character();
}

void keyboard_begin_line(void) {
    buffer_idx = 0;
    edit_cursor = 0;
    rendered_length = 0;
    command_ready = 0;
    history_cursor = -1;
    kbd_buffer[0] = '\0';
}

void keyboard_handler(void) {
    uint8_t scancode = inb(0x60);
    if (scancode == 0xE0) {
        extended_scancode = 1;
        outb(0x20, 0x20);
        return;
    }

    if (scancode == 0x2A || scancode == 0x36) {
        shift_pressed = 1;
        outb(0x20, 0x20);
        return;
    }
    if (scancode == 0xAA || scancode == 0xB6) {
        shift_pressed = 0;
        outb(0x20, 0x20);
        return;
    }

    if (!(scancode & 0x80) && !command_ready) {
        if (extended_scancode) {
            switch (scancode) {
                case 0x4B: if (edit_cursor > 0) { edit_cursor--; vga_move_cursor_left(); } break;
                case 0x4D: if (edit_cursor < buffer_idx) { edit_cursor++; vga_move_cursor_right(); } break;
                case 0x47: while (edit_cursor > 0) { edit_cursor--; vga_move_cursor_left(); } break;
                case 0x4F: while (edit_cursor < buffer_idx) { edit_cursor++; vga_move_cursor_right(); } break;
                case 0x48: history_up(); break;
                case 0x50: history_down(); break;
                /* E0 53 is the extended Delete key; remove at the cursor. */
                case 0x53: delete_character(); break;
            }
        } else {
            char key = kbd_us[scancode];
            if (shift_pressed) {
                key = shifted_key(key);
            }
            if (key == '\n') {
                while (edit_cursor < buffer_idx) {
                    edit_cursor++;
                    vga_move_cursor_right();
                }
                kbd_buffer[buffer_idx] = '\0';
                remember_command();
                command_ready = 1;
                rendered_length = 0;
                print_char('\n');
            }
            else if (key == '\b') {
                /* Backspace removes the character immediately left of the cursor. */
                backspace_character();
            }
            else if (key != 0) {
                insert_character(key);
            }
        }
    }
    extended_scancode = 0;
    outb(0x20, 0x20);
}
