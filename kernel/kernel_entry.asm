[BITS 64]
global _start
extern zero_bss
extern kernel_main

_start:
    ; Kernel entry succeeded; clear stage2's K fault marker before C runs.
    mov word [abs 0xB8000], 0x0F34

    ; The raw kernel binary does not contain .bss bytes, so clear them before C.
    call zero_bss
    call kernel_main

.hang:
    hlt
    jmp .hang
