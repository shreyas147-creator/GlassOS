[BITS 32]
PAYLOAD_TEXT_START equ 0x200000
PAYLOAD_TEXT_END equ 0x280000
PAYLOAD_RODATA_START equ 0x280000
PAYLOAD_RODATA_END equ 0x290000
PAYLOAD_TEXT_PAGES equ (PAYLOAD_TEXT_END - PAYLOAD_TEXT_START) / 0x1000
PAYLOAD_RODATA_PAGES equ (PAYLOAD_RODATA_END - PAYLOAD_RODATA_START) / 0x1000

section .multiboot
align 4
dd 0x1BADB002, 3, -(0x1BADB002+3)
section .text
global multiboot_entry
extern kernel_payload
multiboot_entry:
 cli
 cmp eax,0x2BADB002
 jne halt
 mov ebp,ebx
 call serial_init
 mov esi,banner
 call serial_puts
 mov edi,0x1000
 xor eax,eax
 mov ecx,5120
 rep stosd
 mov dword [0x1000],0x2003
 mov dword [0x2000],0x3003
 mov dword [0x3000],0x4003
 mov dword [0x3008],0x5003
 ; The allocator and Multiboot memory map may reside above the payload.
 mov ecx,2
 mov edi,0x3010
.large: mov eax,ecx
 shl eax,21
 or eax,0x83
 mov [edi],eax
 mov dword [edi+4],0x80000000
 add edi,8
 inc ecx
 cmp ecx,512
 jb .large
 ; map 0..2MiB RW,NX
 xor ecx,ecx
 mov edi,0x4000
.low: mov eax,ecx
 shl eax,12
 or eax,3
 mov [edi],eax
 mov dword [edi+4],0x80000000
 add edi,8
 inc ecx
 cmp ecx,512
 jb .low
 ; The Multiboot loader itself executes at 1 MiB.
 mov dword [0x4800],0x00100001
 mov dword [0x4808],0x00101001
 mov dword [0x4804],0
 mov dword [0x480c],0
 ; map 2..4MiB RW,NX, overlay kernel text RX and rodata R,NX.
 ; Keep these constants aligned with linker.ld assertions.
 mov edi,0x5000
.payload: mov eax,ecx
 shl eax,12
 or eax,3
 mov [edi],eax
 mov dword [edi+4],0x80000000
 add edi,8
 inc ecx
 cmp ecx,1024
 jb .payload
 mov edi,0x5000
 mov eax,PAYLOAD_TEXT_START | 1
 mov ecx,PAYLOAD_TEXT_PAGES
.rx: mov [edi],eax
 mov dword [edi+4],0
 add eax,0x1000
 add edi,8
 loop .rx
 mov eax,PAYLOAD_RODATA_START
 mov ecx,PAYLOAD_RODATA_PAGES
.ro: mov edx,eax
 or edx,1
 mov [edi],edx
 mov dword [edi+4],0x80000000
 add eax,0x1000
 add edi,8
 loop .ro
 mov eax,cr4
 mov dx,0x3f8
 mov al,'P'
 out dx,al
 or eax,1<<5
 mov cr4,eax
 mov dx,0x3f8
 mov al,'4'
 out dx,al
 mov ecx,0xC0000080
 rdmsr
 or eax,(1<<8) | (1<<11)
 wrmsr
 mov dx,0x3f8
 mov al,'E'
 out dx,al
 mov eax,0x1000
 mov cr3,eax
 mov dx,0x3f8
 mov al,'3'
 out dx,al
 lgdt [gdtr]
 mov eax,cr0
 or eax,0x80000001
 mov cr0,eax
 mov dx,0x3f8
 mov al,'J'
 out dx,al
 jmp 0x08:long_mode
[BITS 64]
long_mode: mov ax,0x10
 mov ds,ax
 mov es,ax
 mov ss,ax
 mov fs,ax
 mov gs,ax
 mov rsp,0x2ff000
 and rsp,-16
 mov dx,0x3f8
 mov al,'L'
 out dx,al
 mov rdi,rbp
 mov rax,kernel_payload
 jmp rax
[BITS 32]
serial_init: mov dx,0x3f9
 xor al,al
 out dx,al
 mov dx,0x3fb
 mov al,0x80
 out dx,al
 mov dx,0x3f8
 mov al,1
 out dx,al
 mov dx,0x3f9
 xor al,al
 out dx,al
 mov dx,0x3fb
 mov al,3
 out dx,al
 ret
serial_puts: lodsb
 test al,al
 jz .done
 mov bl,al
 mov dx,0x3fd
.wait: in al,dx
 test al,0x20
 jz .wait
 mov dx,0x3f8
 mov al,bl
 out dx,al
 jmp serial_puts
.done: ret
halt: hlt
 jmp halt
align 8
gdt: dq 0
 dq 0x00209a0000000000
 dq 0x0000920000000000
gdtr: dw gdtr-gdt-1
 dd gdt
banner db 'GlassOS boot: Multiboot v1 handoff',13,10,0
