#include "memory.h"

#define HEAP_START 0x20000
uint64_t heap_ptr = HEAP_START;

void* malloc(uint64_t size) {
    void* ptr = (void*)heap_ptr;
    heap_ptr += size;
    return ptr;
}