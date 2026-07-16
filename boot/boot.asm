[bits 16]
[org 0x7C00]

STAGE2_SECTORS equ 48
KERNEL_SECTORS equ 128

; VGA text attributes: normal progress is white on black; a fatal marker is
; light red on black. A failed mode transition cannot return to an error routine,
; so the last red marker identifies the boundary that did not complete.
VGA_PROGRESS equ 0x0F
VGA_FATAL    equ 0x0C

MEMORY_MAP_ADDR        equ 0x5000
MEMORY_MAP_ENTRIES     equ MEMORY_MAP_ADDR + 8
MEMORY_MAP_MAX_ENTRIES equ 32

start:
    mov [BOOT_DRIVE], dl

    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    mov ax, 0x0003
    int 0x10

    mov ax, 0xB800
    mov es, ax
    mov word [es:0x00], (VGA_PROGRESS << 8) | '1'

    ; Reset ES back to 0 before BIOS disk calls
    xor ax, ax
    mov es, ax

    ; Query the BIOS E820 map while BIOS services are still available. The
    ; kernel reads this fixed, identity-mapped buffer after entering long mode.
    call detect_memory_map

    ; Reset the disk system before issuing extended LBA reads.
    xor ah, ah
    mov dl, [BOOT_DRIVE]
    int 0x13
    jc disk_error

    ; Keep each BIOS transfer inside a 64 KiB DMA window. The image layout is:
    ; sector 0 = boot, sectors 1..48 = stage2, sectors 49..176 = kernel.
    mov word [dap_count], STAGE2_SECTORS
    mov word [dap_offset], 0x9000
    mov word [dap_segment], 0x0000
    mov dword [dap_lba_low], 1
    mov dword [dap_lba_high], 0
    call disk_read

    ; The first 8 kernel sectors fill 0xF000..0xFFFF without crossing 64 KiB.
    mov word [dap_count], 8
    mov word [dap_offset], 0xF000
    mov word [dap_segment], 0x0000
    mov dword [dap_lba_low], 1 + STAGE2_SECTORS
    mov dword [dap_lba_high], 0
    call disk_read

    ; The remaining 120 sectors load contiguously from physical 0x10000.
    mov word [dap_count], KERNEL_SECTORS - 8
    mov word [dap_offset], 0x0000
    mov word [dap_segment], 0x1000
    mov dword [dap_lba_low], 1 + STAGE2_SECTORS + 8
    mov dword [dap_lba_high], 0
    call disk_read

    cli

    ; Leave P visible until stage2 confirms protected-mode entry.
    mov ax, 0xB800
    mov es, ax
    mov word [es:0x00], (VGA_FATAL << 8) | 'P'

    lgdt [gdt_descriptor]

    mov eax, cr0
    or eax, 1
    mov cr0, eax

    jmp 0x08:0x9000

disk_read:
    mov si, dap
    mov dl, [BOOT_DRIVE]
    mov ah, 0x42
    int 0x13
    jc disk_error
    ret

disk_error:
    mov ax, 0xB800
    mov es, ax
    mov word [es:0x00], (VGA_FATAL << 8) | 'D'
    hlt
    jmp $

detect_memory_map:
    xor ax, ax
    mov es, ax
    mov dword [MEMORY_MAP_ADDR], 0
    mov dword [MEMORY_MAP_ADDR + 4], 0
    xor ebx, ebx
    mov di, MEMORY_MAP_ENTRIES

.next_entry:
    mov dword [es:di + 20], 1
    mov eax, 0xE820
    mov edx, 0x534D4150             ; "SMAP"
    mov ecx, 24
    int 0x15
    jc .done
    cmp eax, 0x534D4150
    jne .done

    inc dword [MEMORY_MAP_ADDR]
    add di, 24
    cmp dword [MEMORY_MAP_ADDR], MEMORY_MAP_MAX_ENTRIES
    jae .done
    test ebx, ebx
    jnz .next_entry

.done:
    ret

BOOT_DRIVE: db 0

align 4
dap:
    db 0x10
    db 0
dap_count:
    dw 0
dap_offset:
    dw 0
dap_segment:
    dw 0
dap_lba_low:
    dd 0
dap_lba_high:
    dd 0

align 8
gdt_start:
    dq 0x0000000000000000
    dq 0x00CF9A000000FFFF   ; Code selector 0x08
    dq 0x00CF92000000FFFF   ; Data selector 0x10
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

times 510-($-$$) db 0
dw 0xAA55
