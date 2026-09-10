extern vector_dispatch

section .text
bits 64

%macro VECTOR_ENTRY 1
vector_entry%1:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rdi, %1
    mov rax, rsp
    and rsp, -16
    sub rsp, 16
    mov [rsp], rax
    cld
    call vector_dispatch
    mov rsp, [rsp]

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    iretq
%endmacro

%assign v 0x20
%rep 256 - 0x20
    %if v != 0x80
        VECTOR_ENTRY v
    %endif
    %assign v v + 1
%endrep

section .rodata
align 8
global vector_entries
vector_entries:
%assign v 0
%rep 256
    %if v < 0x20 || v == 0x80
        dq 0
    %else
        dq vector_entry %+ v
    %endif
    %assign v v + 1
%endrep
