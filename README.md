# GlassOS

GlassOS is an inspectable x86_64 kernel. QEMU is the first supported platform and Multiboot v1 is the supported loader contract. Build with `make all`, run the interactive VGA window with `make run`, and execute the headless serial smoke test with `make test-boot`.

The v1 toolchain workflow is `x86_64-elf-gcc`, `x86_64-elf-ld`, NASM, and `qemu-system-x86_64`. `make toolchain` prints the versioned workflow identifier. The build emits a Multiboot ELF, not a fixed-sector disk image.

The shell includes truth-layer probes: `inspect <object>`, `expand <object>`, `why <event-or-object>`, and `history <object>`. Good first objects to try are `system`, `memory`, `page_allocator`, `heap`, `ramfs`, `shell`, `events`, and `/README.TXT`.

Events include stable actor/target object ids, event kinds, parent links, and transaction ids. Shell-visible mutable or inspecting actions now flow through `policy_check` before emitting their linked action/query event.

Use `make test-truth` to drive `cat /README.TXT` through QEMU and verify the command -> policy -> lookup -> read transaction in the serial event log.

See `docs/ARCHITECTURE.md`, `docs/PHILOSOPHY.md`, and the versioned contracts in `docs/contracts/`.