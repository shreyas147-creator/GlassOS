#include "serial.h"
#include "io.h"
#define SERIAL_DATA 0x3F8
#define SERIAL_STATUS 0x3FD
static void put(char c) { while ((inb(SERIAL_STATUS) & 0x20) == 0) {} outb(SERIAL_DATA, (uint8_t)c); }
void serial_write_char(char character) { put(character); }
void serial_write(const char *text) { while (*text) put(*text++); }
void serial_write_hex(uint64_t value) { const char d[] = "0123456789abcdef"; serial_write("0x"); for (int i = 15; i >= 0; --i) put(d[(value >> (i * 4)) & 15]); }
void serial_write_decimal(uint64_t value) { char out[21]; int n = 0; if (!value) { put('0'); return; } while (value) { out[n++] = (char)('0' + value % 10); value /= 10; } while (n) put(out[--n]); }
