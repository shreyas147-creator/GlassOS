#ifndef MEMORY_H
#define MEMORY_H
#include "types.h"

#define PAGE_SIZE 4096ULL
#define PHYSICAL_MEMORY_LIMIT 0x40000000ULL

extern uint64_t heap_ptr;
int memory_init(uint32_t multiboot_info_address);
uint64_t memory_total_usable(void);
uint32_t memory_map_entry_count(void);
uint64_t memory_mapped_limit(void);
uint64_t page_alloc(void);
uint64_t page_alloc_contiguous(uint64_t page_count);
int memory_map_user_page(uint64_t physical_address, int executable);
void page_free(uint64_t physical_address);
uint64_t page_allocator_free_pages(void);
void* malloc(uint64_t size);
uint64_t heap_remaining(void);
uint64_t heap_capacity(void);

#endif
