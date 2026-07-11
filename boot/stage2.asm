[BITS 32]
[ORG 0x9000]

VGA_PROGRESS equ 0x0F
VGA_FATAL    equ 0x0C

init_pm:
    cli

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, stack_top

    ; Protected-mode entry succeeded; clear the P fault marker from stage1.
    mov word [0xB8000], (VGA_PROGRESS << 8) | '2'

    ; 1. Configure Paging
    mov eax, pdpt_table
    or eax, 0x3
    mov [pml4_table], eax

    mov eax, pde_table
    or eax, 0x3
    mov [pdpt_table], eax

    mov edi, pde_table
    mov eax, 0x00000000
    or eax, 0x83                ; 2MB Huge Page
    mov [edi], eax

    mov eax, pml4_table
    mov cr3, eax

    mov eax, cr4
    or eax, (1 << 5)
    mov cr4, eax

    mov ecx, 0xC0000080
    rdmsr
    or eax, (1 << 8)
    wrmsr

    lgdt [gdt_descriptor]

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax

    mov eax, cr0
    or eax, (1 << 31)

    ; Leave L visible until the 64-bit entry point executes.
    mov word [0xB8000], (VGA_FATAL << 8) | 'L'
    mov cr0, eax

    jmp 0x18:long_mode_start

[BITS 64]
long_mode_start:
    ; Load a valid 64-bit data segment selector (0x20) into segments
    mov ax, 0x20
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Long-mode entry succeeded; clear the L fault marker.
    mov word [abs 0xB8000], (VGA_PROGRESS << 8) | '3'

    ; Jump into the concatenated C kernel payload at 0xF000
    ; Leave K visible until the kernel entry stub runs.
    mov word [abs 0xB8000], (VGA_FATAL << 8) | 'K'
    mov rax, 0xF000
    jmp rax

hang:
    hlt
    jmp hang

; =============================================================================
; Global Descriptor Table Setup
; =============================================================================
align 8
gdt_start:
    dq 0x0000000000000000       ; 0x00: Null descriptor
    dq 0x00CF9A000000FFFF       ; 0x08: 32-bit Code Descriptor
    dq 0x00CF92000000FFFF       ; 0x10: 32-bit Data Descriptor
    dq 0x00209A0000000000       ; 0x18: 64-bit Code Descriptor (Long mode flag)
    dq 0x0000920000000000       ; 0x20: 64-bit Data Descriptor
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

align 4096
pml4_table:    times 4096 db 0
pdpt_table:    times 4096 db 0
pde_table:     times 4096 db 0

align 16
stack_bottom:
    times 4096 db 0
stack_top:
