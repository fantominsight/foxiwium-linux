; Foxiwium OS - ISR stubs for IDT
; Generates stubs for all 256 interrupt vectors

bits 64
section .text

%macro ISR_NOERRCODE 1
global isr%1
isr%1:
    push qword 0               ; dummy error code
    push qword %1              ; interrupt number
    jmp isr_common
%endmacro

%macro ISR_ERRCODE 1
global isr%1
isr%1:
    push qword %1              ; interrupt number
    jmp isr_common
%endmacro

; Exceptions (0-31)
ISR_NOERRCODE  0
ISR_NOERRCODE  1
ISR_NOERRCODE  2
ISR_NOERRCODE  3
ISR_NOERRCODE  4
ISR_NOERRCODE  5
ISR_NOERRCODE  6
ISR_NOERRCODE  7
ISR_ERRCODE    8
ISR_NOERRCODE  9
ISR_ERRCODE   10
ISR_ERRCODE   11
ISR_ERRCODE   12
ISR_ERRCODE   13
ISR_ERRCODE   14
ISR_NOERRCODE 15
ISR_NOERRCODE 16
ISR_ERRCODE   17
ISR_NOERRCODE 18
ISR_NOERRCODE 19
ISR_NOERRCODE 20
ISR_ERRCODE   21
ISR_NOERRCODE 22
ISR_NOERRCODE 23
ISR_NOERRCODE 24
ISR_NOERRCODE 25
ISR_NOERRCODE 26
ISR_NOERRCODE 27
ISR_ERRCODE   28
ISR_NOERRCODE 29
ISR_ERRCODE   30
ISR_NOERRCODE 31

; IRQs (32-47)
%assign i 32
%rep 16
    ISR_NOERRCODE i
%assign i i+1
%endrep

; Software interrupts and spurious (48-255)
%assign i 48
%rep 208
    ISR_NOERRCODE i
%assign i i+1
%endrep

; Common ISR handler
isr_common:
    ; Save all general purpose registers
    push rax
    push rbx
    push rcx
    push rdx
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

    ; Call C++ handler
    mov rdi, rsp            ; arg1 = pointer to InterruptFrame
    extern idt_dispatch
    call idt_dispatch

    ; Restore registers
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
    pop rdx
    pop rcx
    pop rbx
    pop rax

    ; Pop error code and interrupt number
    add rsp, 16

    iretq

; Export stub table
section .data
global isr_stub_table
isr_stub_table:
%assign i 0
%rep 256
    dq isr%+i
%assign i i+1
%endrep
