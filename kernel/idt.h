#ifndef IDT_H
#define IDT_H

#include "types.h"

void init_idt(void);
int exception_handler(uint64_t vector, uint64_t error_code, void *frame);

#endif
