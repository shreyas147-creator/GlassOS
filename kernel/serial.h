#ifndef SERIAL_H
#define SERIAL_H
#include "types.h"
void serial_write(const char *text);
void serial_write_char(char character);
void serial_write_hex(uint64_t value);
void serial_write_decimal(uint64_t value);
#endif
