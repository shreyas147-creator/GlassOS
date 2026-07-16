# GlassOS architecture map (v1)

`kernel/multiboot_entry.asm` validates the Multiboot handoff, initializes COM1 before C runs, builds the bootstrap page tables, and enters 64-bit C. The mapping is identity-mapped: kernel text is RX, rodata is R/NX, data/bss/stack and page tables are RW/NX; remaining physical RAM uses RW/NX large pages. `kernel/event.c` is the bounded early event journal and serial mirror. The IDT, timer, keyboard, memory allocator, RAM filesystem, and shell are independent kernel services.

QEMU starts the ELF with `-kernel`; there is no raw-sector layout or firmware-specific loader in the supported path.
