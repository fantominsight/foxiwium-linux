[bits 64]

global syscall_entry
extern syscall_entry_handler

section .text

syscall_entry:
    swapgs
    mov [gs:0x10], rsp
    mov rsp, [gs:0x08]

    push rcx
    push r11
    push rbp

    mov rcx, r10
    push rax
    call syscall_entry_handler
    add rsp, 8

    pop rbp
    pop r11
    pop rcx
    mov rsp, [gs:0x10]
    swapgs
    o64 sysret
