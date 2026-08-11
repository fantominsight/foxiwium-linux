[bits 64]
[org 0x400000]

section .text

_start:
    mov rax, 1
    mov rdi, 1
    lea rsi, [rel welcome]
    mov rdx, welcome_len
    syscall

.prompt:
    mov rax, 1
    mov rdi, 1
    lea rsi, [rel prompt]
    mov rdx, prompt_len
    syscall

    xor r8, r8

.readloop:
    mov rax, 0
    mov rdi, 0
    lea rsi, [rel readbuf]
    mov rdx, 1
    syscall

    test rax, rax
    jz .readloop

    mov cl, byte [rel readbuf]
    cmp cl, 10
    je .exec
    cmp cl, 13
    je .exec

    cmp r8, 126
    jae .exec

    lea rdi, [rel input_buf]
    mov byte [rdi + r8], cl
    inc r8

    mov rax, 1
    mov rdi, 1
    lea rsi, [rel readbuf]
    mov rdx, 1
    syscall

    jmp .readloop

.exec:
    lea rdi, [rel input_buf]
    mov byte [rdi + r8], 0
    test r8, r8
    jz .prompt

    mov rax, 1
    mov rdi, 1
    lea rsi, [rel newline]
    mov rdx, 1
    syscall

    lea rdi, [rel input_buf]
    lea rsi, [rel cmd_help]
    call strcmp
    test rax, rax
    jnz .do_help

    lea rdi, [rel input_buf]
    lea rsi, [rel cmd_exit]
    call strcmp
    test rax, rax
    jnz .do_exit

    lea rdi, [rel input_buf]
    lea rsi, [rel cmd_hello]
    call strcmp
    test rax, rax
    jnz .do_hello

    mov rax, 1
    mov rdi, 1
    lea rsi, [rel unknown]
    mov rdx, unknown_len
    syscall
    jmp .prompt

.do_help:
    mov rax, 1
    mov rdi, 1
    lea rsi, [rel help_text]
    mov rdx, help_len
    syscall
    jmp .prompt

.do_hello:
    mov rax, 1
    mov rdi, 1
    lea rsi, [rel hello_text]
    mov rdx, hello_len
    syscall
    jmp .prompt

.do_exit:
    mov rax, 6
    xor rdi, rdi
    syscall

strcmp:
    xor rax, rax
.lp:
    movzx ecx, byte [rdi + rax]
    movzx edx, byte [rsi + rax]
    cmp cl, dl
    jne .neq
    test cl, cl
    jz .eq
    inc rax
    jmp .lp
.neq:
    mov rax, 1
    ret
.eq:
    xor rax, rax
    ret

section .data

welcome:     db "=== Foxiwium Shell v0.3 ===", 10, 0
welcome_len: equ $ - welcome
prompt:      db "fox$ "
prompt_len:  equ $ - prompt
newline:     db 10
unknown:     db "Unknown command", 10, 0
unknown_len: equ $ - unknown
help_text:   db "Commands: help, hello, exit", 10, 0
help_len:    equ $ - help_text
hello_text:  db "Hello from userspace!", 10, 0
hello_len:   equ $ - hello_text
cmd_help:    db "help", 0
cmd_exit:    db "exit", 0
cmd_hello:   db "hello", 0

section .bss
readbuf: resb 1
input_buf: resb 128
