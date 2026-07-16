[BITS 32]
section .multiboot
align 4
dd 0x1BADB002
dd 0x00000003
dd -(0x1BADB002 + 0x00000003)

section .text.prologue
global multiboot_entry
extern __text_start, __text_end, __rodata_start, __rodata_end
extern __stack_top, zero_bss, kernel_main

multiboot_entry:
    cli
    cmp eax, 0x2BADB002
    jne boot_halt
    mov esi, ebx
    call serial_init
    mov esi, boot_banner
    call serial_puts
    mov edi, 0x1000
    xor eax, eax
    mov ecx, 4096
    rep stosd
    mov dword [0x1000], 0x2003
    mov dword [0x2000], 0x3003
    mov dword [0x3000], 0x4003
    ; PD entries 1..511: 2 MiB RW,NX mappings. Entry 0 is 4 KiB pages.
    mov ecx, 1
    mov edi, 0x3008
.huge:
    mov eax, ecx
    shl eax, 21
    or eax, dword 0x80000083
    mov [edi], eax
    add edi, 8
    inc ecx
    cmp ecx, 512
    jb .huge
    ; Low 1 MiB: loader data, tables and VGA, all RW,NX.
    xor ecx, ecx
    mov edi, 0x4000
.low:
    mov eax, ecx
    shl eax, 12
    or eax, dword 0x80000003
    mov [edi], eax
    add edi, 8
    inc ecx
    cmp ecx, 256
    jb .low
    ; Kernel defaults to RW,NX, then text and rodata are tightened below.
    mov ecx, 256
.kernel_default:
    mov eax, ecx
    shl eax, 12
    or eax, dword 0x80000003
    mov [edi], eax
    add edi, 8
    inc ecx
    cmp ecx, 512
    jb .kernel_default
    mov edi, __text_start
    mov ebp, __text_end
    call map_rx_range
    mov edi, __rodata_start
    mov ebp, __rodata_end
    call map_rnx_range
    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax
    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    wrmsr
    mov eax, 0x1000
    mov cr3, eax
    lgdt [gdt_pointer]
    mov eax, cr0
    or eax, 0x80000001
    mov cr0, eax
    jmp 0x08:long_mode

; edi=start and ebp=end, both page aligned. PTE index is physical / 4KiB.
map_rx_range:
    mov eax, edi
    shr eax, 9
    add eax, 0x4000
.loop:
    mov edx, edi
    or edx, 1
    mov [eax], edx
    add edi, 0x1000
    add eax, 8
    cmp edi, ebp
    jb .loop
    ret
map_rnx_range:
    mov eax, edi
    shr eax, 9
    add eax, 0x4000
.loop:
    mov edx, edi
    or edx, dword 0x80000001
    mov [eax], edx
    add edi, 0x1000
    add eax, 8
    cmp edi, ebp
    jb .loop
    ret

[BITS 64]
long_mode:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov rsp, __stack_top
    and rsp, -16
    mov rdi, rsi
    call zero_bss
    call kernel_main
boot_halt: hlt
    jmp boot_halt

[BITS 32]
serial_init:
    mov dx, 0x3F9
    xor al, al
    out dx, al
    mov dx, 0x3FB
    mov al, 0x80
    out dx, al
    mov dx, 0x3F8
    mov al, 1
    out dx, al
    mov dx, 0x3F9
    xor al, al
    out dx, al
    mov dx, 0x3FB
    mov al, 3
    out dx, al
    ret
serial_puts:
    lodsb
    test al, al
    jz .done
    mov bl, al
    mov dx, 0x3FD
.wait: in al, dx
    test al, 0x20
    jz .wait
    mov dx, 0x3F8
    mov al, bl
    out dx, al
    jmp serial_puts
.done: ret
align 8
gdt: dq 0
     dq 0x00209A0000000000
     dq 0x0000920000000000
gdt_pointer: dw gdt_pointer - gdt - 1
             dd gdt
boot_banner db "GlassOS boot: Multiboot v1 handoff", 13, 10, 0
