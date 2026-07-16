#include "memory.h"
#include "event.h"
#include "object.h"

#define IDENTITY_MAP_LIMIT PHYSICAL_MEMORY_LIMIT
#define PAGE_ALLOCATOR_START 0x100000ULL
#define PAGE_COUNT (IDENTITY_MAP_LIMIT / PAGE_SIZE)
#define PAGE_BITMAP_SIZE (PAGE_COUNT / 8)
#define HEAP_PAGE_COUNT 16
#define LOADER_RESERVED_START 0x100000ULL
#define LOADER_RESERVED_END 0x200000ULL
#define BOOT_STACK_RESERVED_START 0x2F0000ULL
#define BOOT_STACK_RESERVED_END 0x300000ULL

static uint8_t page_bitmap[PAGE_BITMAP_SIZE];
static uint8_t usable_bitmap[PAGE_BITMAP_SIZE];
static uint64_t usable_memory_bytes = 0;
static uint32_t detected_entry_count = 0;
static uint64_t heap_base = 0;
static uint64_t heap_limit = 0;
static int allocator_ready = 0;

uint64_t heap_ptr = 0;

extern uint8_t __text_start[];
extern uint8_t __bss_end[];

struct multiboot_info { uint32_t flags, mem_lower, mem_upper, boot_device, cmdline, mods_count, mods_addr; uint32_t syms[4]; uint32_t mmap_length, mmap_addr; } __attribute__((packed));
struct multiboot_mmap { uint32_t size; uint64_t base, length; uint32_t type; } __attribute__((packed));

static void bitmap_set(uint64_t page_index) {
    page_bitmap[page_index / 8] |= (uint8_t)(1U << (page_index % 8));
}

static void bitmap_clear(uint64_t page_index) {
    page_bitmap[page_index / 8] &= (uint8_t)~(1U << (page_index % 8));
}

static int bitmap_is_set(uint64_t page_index) {
    return (page_bitmap[page_index / 8] & (uint8_t)(1U << (page_index % 8))) != 0;
}

static void usable_bitmap_set(uint64_t page_index) {
    usable_bitmap[page_index / 8] |= (uint8_t)(1U << (page_index % 8));
}

static void usable_bitmap_clear(uint64_t page_index) {
    usable_bitmap[page_index / 8] &= (uint8_t)~(1U << (page_index % 8));
}

static int usable_bitmap_is_set(uint64_t page_index) {
    return (usable_bitmap[page_index / 8] & (uint8_t)(1U << (page_index % 8))) != 0;
}

static uint64_t align_up(uint64_t value) {
    return (value + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

static uint64_t align_down(uint64_t value) {
    return value & ~(PAGE_SIZE - 1);
}

static void bitmap_fill(uint8_t *bitmap, uint8_t value) {
    for (uint64_t i = 0; i < PAGE_BITMAP_SIZE; i++) {
        bitmap[i] = value;
    }
}

static void mark_usable_range(uint64_t base, uint64_t length) {
    if (base >= IDENTITY_MAP_LIMIT || length == 0) {
        return;
    }

    uint64_t end = length > IDENTITY_MAP_LIMIT - base ? IDENTITY_MAP_LIMIT : base + length;
    uint64_t start = base < PAGE_ALLOCATOR_START ? PAGE_ALLOCATOR_START : base;
    start = align_up(start);
    end = align_down(end);

    for (uint64_t address = start; address < end; address += PAGE_SIZE) {
        usable_bitmap_set(address / PAGE_SIZE);
        bitmap_clear(address / PAGE_SIZE);
    }
}

/* A non-usable E820 record wins over an overlapping usable record. */
static void mark_reserved_range(uint64_t base, uint64_t length) {
    if (base >= IDENTITY_MAP_LIMIT || length == 0) {
        return;
    }

    uint64_t end = length > IDENTITY_MAP_LIMIT - base ? IDENTITY_MAP_LIMIT : base + length;
    uint64_t start = base < PAGE_ALLOCATOR_START ? PAGE_ALLOCATOR_START : base;
    start = align_down(start);
    end = align_up(end);
    if (end > IDENTITY_MAP_LIMIT) {
        end = IDENTITY_MAP_LIMIT;
    }

    for (uint64_t address = start; address < end; address += PAGE_SIZE) {
        uint64_t page = address / PAGE_SIZE;
        usable_bitmap_clear(page);
        bitmap_set(page);
    }
}

static void reserve_address_range(uint64_t start, uint64_t end) {
    if (end <= start) {
        return;
    }
    mark_reserved_range(start, end - start);
}

static void reserve_kernel_ranges(uint32_t multiboot_info_address,
                                  uint32_t mmap_addr,
                                  uint32_t mmap_length) {
    reserve_address_range(LOADER_RESERVED_START, LOADER_RESERVED_END);
    reserve_address_range((uint64_t)__text_start, (uint64_t)__bss_end);
    reserve_address_range(BOOT_STACK_RESERVED_START, BOOT_STACK_RESERVED_END);
    reserve_address_range(multiboot_info_address,
                          multiboot_info_address + (uint32_t)sizeof(struct multiboot_info));
    reserve_address_range(mmap_addr, mmap_addr + mmap_length);
}

static void recount_usable_memory(void) {
    usable_memory_bytes = 0;
    for (uint64_t page = PAGE_ALLOCATOR_START / PAGE_SIZE; page < PAGE_COUNT; page++) {
        if (usable_bitmap_is_set(page)) {
            usable_memory_bytes += PAGE_SIZE;
        }
    }
}

static int range_is_free(uint64_t first_page, uint64_t page_count) {
    if (first_page > PAGE_COUNT || page_count > PAGE_COUNT - first_page) {
        return 0;
    }
    for (uint64_t page = 0; page < page_count; page++) {
        if (bitmap_is_set(first_page + page)) {
            return 0;
        }
    }
    return 1;
}

int memory_init(uint32_t multiboot_info_address) {
    const volatile struct multiboot_info *info = (const volatile struct multiboot_info *)(uint64_t)multiboot_info_address;
    if (multiboot_info_address == 0 ||
        (info->flags & (1U << 6)) == 0 ||
        info->mmap_length == 0 ||
        info->mmap_addr == 0 ||
        info->mmap_addr >= IDENTITY_MAP_LIMIT ||
        info->mmap_length > IDENTITY_MAP_LIMIT - info->mmap_addr) {
        event_record("memory", "memory", "parse_multiboot", EVENT_FAULT, event_current(), "deny:invalid-memory-map");
        return 0;
    }

    /* Start fully reserved; E820 type-1 ranges selectively become allocable. */
    bitmap_fill(page_bitmap, 0xFF);
    bitmap_fill(usable_bitmap, 0);

    detected_entry_count = 0;
    usable_memory_bytes = 0;
    heap_ptr = 0;
    heap_base = 0;
    heap_limit = 0;
    allocator_ready = 0;
    const struct kernel_object *memory = object_find("memory");
    uint32_t memory_id = memory == 0 ? 0 : memory->id;
    object_register("allocator", "page_allocator", memory_id, "initializing", event_current());
    object_register("heap", "heap", memory_id, "initializing", event_current());

    uint32_t offset = 0;
    while (offset < info->mmap_length) {
        const volatile struct multiboot_mmap *entry = (const volatile struct multiboot_mmap *)(uint64_t)(info->mmap_addr + offset);
        if (info->mmap_length - offset < 24 ||
            entry->size < 20 ||
            entry->size > info->mmap_length - offset - 4) {
            event_record("memory", "memory", "parse_multiboot", EVENT_FAULT, event_current(), "deny:bad-memory-map-entry");
            return 0;
        }
        detected_entry_count++;
        if (entry->type == 1 && entry->length != 0) {
            mark_usable_range(entry->base, entry->length);
        }
        offset += entry->size + 4;
    }

    /* E820 records should not overlap, but reserve conflicting descriptors
       defensively so a firmware bug cannot expose device memory to callers. */
    offset = 0;
    while (offset < info->mmap_length) {
        const volatile struct multiboot_mmap *entry = (const volatile struct multiboot_mmap *)(uint64_t)(info->mmap_addr + offset);
        if (entry->type != 1 || entry->length == 0) {
            mark_reserved_range(entry->base, entry->length);
        }
        offset += entry->size + 4;
    }

    reserve_kernel_ranges(multiboot_info_address, info->mmap_addr, info->mmap_length);
    recount_usable_memory();

    allocator_ready = 1;
    uint64_t heap_start = page_alloc_contiguous(HEAP_PAGE_COUNT);
    if (heap_start == 0) {
        event_record("memory", "heap", "allocate_pages", EVENT_FAULT, event_current(), "deny:no-pages");
        allocator_ready = 0;
        return 0;
    }
    object_update_state("page_allocator", "ready");
    object_update_state("heap", "ready");
    event_record("memory", "heap", "allocate_pages", EVENT_OK, event_current(), "allow:bootstrap-heap");
    heap_ptr = heap_base = heap_start;
    heap_limit = heap_start + HEAP_PAGE_COUNT * PAGE_SIZE;
    event_record("memory", "memory", "parse_multiboot", EVENT_OK, event_current(), "allow:real-memory-map");
    return 1;
}

uint64_t memory_total_usable(void) {
    return usable_memory_bytes;
}

uint32_t memory_map_entry_count(void) {
    return detected_entry_count;
}

uint64_t memory_mapped_limit(void) {
    return IDENTITY_MAP_LIMIT;
}

int memory_map_user_page(uint64_t physical_address, int executable) {
    if ((physical_address & (PAGE_SIZE - 1)) != 0 || physical_address >= 0x400000ULL) {
        event_record("memory", "page_tables", "map_user", EVENT_DENIED,
                     event_current(), "deny:user-page-out-of-bootstrap-range");
        return 0;
    }

    volatile uint64_t *pml4 = (volatile uint64_t *)0x1000;
    volatile uint64_t *pdpt = (volatile uint64_t *)0x2000;
    volatile uint64_t *pd = (volatile uint64_t *)0x3000;
    volatile uint64_t *pt = physical_address < 0x200000ULL ?
                            (volatile uint64_t *)0x4000 :
                            (volatile uint64_t *)0x5000;
    uint64_t pd_index = physical_address >> 21;
    uint64_t pt_index = (physical_address >> 12) & 0x1FF;
    uint64_t flags = 0x7ULL;
    if (!executable) {
        flags |= 0x8000000000000000ULL;
    }

    pml4[0] |= 0x4ULL;
    pdpt[0] |= 0x4ULL;
    pd[pd_index] |= 0x4ULL;
    pt[pt_index] = physical_address | flags;
    __asm__ volatile ("mov %%cr3, %%rax; mov %%rax, %%cr3" ::: "rax", "memory");
    event_record("memory", "page_tables", "map_user", EVENT_OK,
                 event_current(), executable ? "allow:user-code-page" : "allow:user-stack-page");
    return 1;
}

uint64_t page_alloc_contiguous(uint64_t page_count) {
    if (!allocator_ready || page_count == 0 || page_count > PAGE_COUNT) {
        event_record("memory", "page_allocator", "allocate", EVENT_DENIED, event_current(), "deny:invalid-request");
        return 0;
    }

    for (uint64_t page = PAGE_ALLOCATOR_START / PAGE_SIZE;
         page <= PAGE_COUNT - page_count;
         page++) {
        if (!range_is_free(page, page_count)) {
            continue;
        }
        for (uint64_t offset = 0; offset < page_count; offset++) {
            bitmap_set(page + offset);
        }
        event_record("memory", "page_allocator", "allocate", EVENT_OK, event_current(), "allow:page-owner");
        return page * PAGE_SIZE;
    }
    event_record("memory", "page_allocator", "allocate", EVENT_DENIED, event_current(), "deny:out-of-pages");
    return 0;
}

uint64_t page_alloc(void) {
    return page_alloc_contiguous(1);
}

void page_free(uint64_t physical_address) {
    if ((physical_address & (PAGE_SIZE - 1)) != 0 ||
        physical_address < PAGE_ALLOCATOR_START ||
        physical_address >= IDENTITY_MAP_LIMIT ||
        (physical_address >= heap_base && physical_address < heap_limit) ||
        !usable_bitmap_is_set(physical_address / PAGE_SIZE)) {
        event_record("memory", "page_allocator", "free", EVENT_DENIED, event_current(), "deny:not-owned-page");
        return;
    }
    bitmap_clear(physical_address / PAGE_SIZE);
    event_record("memory", "page_allocator", "free", EVENT_OK, event_current(), "allow:page-owner");
}

uint64_t page_allocator_free_pages(void) {
    uint64_t free_pages = 0;
    for (uint64_t page = PAGE_ALLOCATOR_START / PAGE_SIZE; page < PAGE_COUNT; page++) {
        if (usable_bitmap_is_set(page) && !bitmap_is_set(page)) {
            free_pages++;
        }
    }
    return free_pages;
}

void* malloc(uint64_t size) {
    if (size == 0 || heap_base == 0 || heap_limit == 0 || heap_ptr < heap_base ||
        heap_ptr > heap_limit) {
        event_record("memory", "heap", "allocate", EVENT_DENIED, event_current(), "deny:heap-not-ready");
        return 0;
    }

    /* Reject both capacity exhaustion and the alignment overflow case. */
    if (size > UINT64_MAX - 15) {
        event_record("memory", "heap", "allocate", EVENT_DENIED, event_current(), "deny:size-overflow");
        return 0;
    }
    size = (size + 15) & ~15ULL;
    if (size > heap_limit - heap_ptr) {
        event_record("memory", "heap", "allocate", EVENT_DENIED, event_current(), "deny:heap-exhausted");
        return 0;
    }

    void *ptr = (void *)heap_ptr;
    heap_ptr += size;
    event_record("memory", "heap", "allocate", EVENT_OK, event_current(), "allow:heap");
    return ptr;
}

uint64_t heap_remaining(void) {
    return heap_limit >= heap_ptr ? heap_limit - heap_ptr : 0;
}

uint64_t heap_capacity(void) {
    return heap_limit == 0 ? 0 : HEAP_PAGE_COUNT * PAGE_SIZE;
}
