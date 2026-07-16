[BITS 64]
global _start
extern zero_bss
extern kernel_main

_start:
    mov r12, rdi
    mov dx, 0x3F8
    mov al, 'K'
    out dx, al
    ; Kernel entry succeeded; clear stage2's K fault marker before C runs.
    mov word [abs 0xB8000], 0x0F34

    ; The raw kernel binary does not contain .bss bytes, so clear them before C.
    call zero_bss
    mov rdi, r12
    call kernel_main

.hang:
    hlt
    jmp .hang
