[BITS 64]

global keyboard_isr_wrapper
global timer_isr_wrapper
global syscall_isr_wrapper
global exception_stub_table
global user_enter_ring3
global user_return_to_kernel
extern keyboard_handler
extern timer_handler
extern exception_handler
extern user_syscall_trap
extern user_kernel_return_rsp
extern user_kernel_return_rip

; CPU exception stubs normalize the CPU frame to [vector, error code].
; Exceptions without a CPU-provided error code receive a synthetic zero.
%macro EXCEPTION_NO_ERROR 1
exception_%1:
    push qword 0
    push qword %1
    jmp exception_common
%endmacro

%macro EXCEPTION_WITH_ERROR 1
exception_%1:
    push qword %1
    jmp exception_common
%endmacro

EXCEPTION_NO_ERROR   0
EXCEPTION_NO_ERROR   1
EXCEPTION_NO_ERROR   2
EXCEPTION_NO_ERROR   3
EXCEPTION_NO_ERROR   4
EXCEPTION_NO_ERROR   5
EXCEPTION_NO_ERROR   6
EXCEPTION_NO_ERROR   7
EXCEPTION_WITH_ERROR 8
EXCEPTION_NO_ERROR   9
EXCEPTION_WITH_ERROR 10
EXCEPTION_WITH_ERROR 11
EXCEPTION_WITH_ERROR 12
EXCEPTION_WITH_ERROR 13
EXCEPTION_WITH_ERROR 14
EXCEPTION_NO_ERROR   15
EXCEPTION_NO_ERROR   16
EXCEPTION_WITH_ERROR 17
EXCEPTION_NO_ERROR   18
EXCEPTION_NO_ERROR   19
EXCEPTION_NO_ERROR   20
EXCEPTION_WITH_ERROR 21
EXCEPTION_NO_ERROR   22
EXCEPTION_NO_ERROR   23
EXCEPTION_NO_ERROR   24
EXCEPTION_NO_ERROR   25
EXCEPTION_NO_ERROR   26
EXCEPTION_NO_ERROR   27
EXCEPTION_NO_ERROR   28
EXCEPTION_WITH_ERROR 29
EXCEPTION_WITH_ERROR 30
EXCEPTION_NO_ERROR   31

; The CPU frame leaves RSP 8-byte aligned here. Saving 15 registers flips it
; to the SysV call alignment expected immediately before calling C.
exception_common:
    push rax
    push rcx
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rdi, [rsp + 120]        ; vector
    mov rsi, [rsp + 128]        ; CPU or synthetic error code
    lea rdx, [rsp + 136]        ; interrupt frame: rip, cs, rflags, rsp, ss
    call exception_handler
    test rax, rax
    jnz user_return_to_kernel

    ; exception_handler does not return, but keep the unwind correct.
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    pop rdx
    pop rcx
    pop rax
    add rsp, 16
    iretq

align 8
exception_stub_table:
    dq exception_0, exception_1, exception_2, exception_3
    dq exception_4, exception_5, exception_6, exception_7
    dq exception_8, exception_9, exception_10, exception_11
    dq exception_12, exception_13, exception_14, exception_15
    dq exception_16, exception_17, exception_18, exception_19
    dq exception_20, exception_21, exception_22, exception_23
    dq exception_24, exception_25, exception_26, exception_27
    dq exception_28, exception_29, exception_30, exception_31

timer_isr_wrapper:
    ; Interrupt entry plus 15 saved registers gives the C call ABI alignment.
    push rax
    push rcx
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    call timer_handler

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    pop rdx
    pop rcx
    pop rax
    iretq

keyboard_isr_wrapper:
    ; Match the System V x86-64 ABI before calling C from an interrupt gate.
    push rax
    push rcx
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    call keyboard_handler

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    pop rdx
    pop rcx
    pop rax
    iretq

syscall_isr_wrapper:
    push rax
    push rcx
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rdi, [rsp + 112]        ; saved user rax = syscall number
    mov rsi, [rsp + 128]        ; saved user cs
    call user_syscall_trap

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    pop rdx
    pop rcx
    pop rax
    iretq

; rdi=user rip, rsi=user rsp. Save the kernel continuation and enter CPL3.
user_enter_ring3:
    lea rax, [rel .returned]
    mov [rel user_kernel_return_rip], rax
    mov [rel user_kernel_return_rsp], rsp
    mov ax, 0x1B                ; user data selector, RPL 3
    mov ds, ax
    mov es, ax
    push qword 0x1B
    push rsi
    pushfq
    pop rax
    or rax, 0x200
    push rax
    push qword 0x23             ; user code selector, RPL 3
    push rdi
    iretq
.returned:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    ret

user_return_to_kernel:
    mov rsp, [rel user_kernel_return_rsp]
    mov rax, 1
    jmp [rel user_kernel_return_rip]
