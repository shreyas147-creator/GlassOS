#ifndef MEMORY_H
#define MEMORY_H
#include "types.h"

extern uint64_t heap_ptr;
void* malloc(uint64_t size);

#endif