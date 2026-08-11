; Foxiwium OS - Multiboot2 Boot (32-bit -> 64-bit Long Mode)
; Requests framebuffer via multiboot2 header tags

bits 32

; ====== Multiboot2 Header ======
section .multiboot2
    align 8

mb2_header_start:
    dd 0xe85250d6                ; magic
    dd 0                         ; architecture: i386 protected mode
    dd mb2_header_end - mb2_header_start ; header length
    dd 0x100000000 - (0xe85250d6 + 0 + (mb2_header_end - mb2_header_start))

    ; Framebuffer tag - request linear graphics mode
    align 8
fb_tag_start:
    dw 5                         ; type: framebuffer
    dw 0                         ; flags
    dd fb_tag_end - fb_tag_start ; size
    dd 1280                      ; width (preference)
    dd 720                       ; height (preference)
    dd 32                        ; bpp
fb_tag_end:

    ; End tag
    align 8
    dw 0
    dw 0
    dd 8

mb2_header_end:

; ====== Debug macro ======
%macro dbg 1
    push eax
    push dx
    mov al, %1
    mov dx, 0xE9
    out dx, al
    pop dx
    pop eax
%endmacro

; ====== 32-bit Entry ======
section .text
global _start32
extern kernel_main64

_start32:
    cli

    dbg 'A'

    ; Save multiboot info
    mov [mb2_magic32], eax
    mov [mb2_info32], ebx

    dbg 'B'

    ; Check CPUID
    pushfd
    pop eax
    mov ecx, eax
    xor eax, (1 << 21)
    push eax
    popfd
    pushfd
    pop eax
    push ecx
    popfd
    cmp eax, ecx
    je .no_long_mode

    dbg 'C'

    ; Extended CPUID
    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb .no_long_mode

    dbg 'D'

    ; Long mode
    mov eax, 0x80000001
    cpuid
    test edx, (1 << 29)
    jz .no_long_mode

    dbg 'E'

    ; Clear page tables (0x1000 - 0x6FFF = 24KB for 4GB mapping)
    mov edi, 0x1000
    xor eax, eax
    mov ecx, 6144
    rep stosd

    ; PML4[0] -> PDPT at 0x2000
    mov dword [0x1000], 0x00002003
    mov dword [0x1004], 0

    ; PDPT -> 4 Page Directories (each maps 1GB with 2MB pages)
    mov dword [0x2000], 0x00003003  ; PD0: 0-1GB
    mov dword [0x2008], 0x00004003  ; PD1: 1-2GB
    mov dword [0x2010], 0x00005003  ; PD2: 2-3GB
    mov dword [0x2018], 0x00006003  ; PD3: 3-4GB

    ; Fill all 4 PDs: 2048 entries x 2MB = 4GB identity mapped
    mov edi, 0x3000
    mov eax, 0x00000083
    xor ecx, ecx
.fill_all_pd:
    mov [edi], eax
    mov dword [edi+4], 0
    add eax, 0x00200000
    add edi, 8
    inc ecx
    cmp ecx, 2048
    jb .fill_all_pd

    dbg 'F'

    ; CR3 = PML4
    mov eax, 0x1000
    mov cr3, eax

    dbg 'G'

    ; PAE
    mov eax, cr4
    or eax, (1 << 5)
    mov cr4, eax

    dbg 'H'

    ; LME
    mov ecx, 0xC0000080
    rdmsr
    or eax, (1 << 8)
    wrmsr

    dbg 'I'

    ; Paging
    mov eax, cr0
    or eax, (1 << 31) | (1 << 0)
    mov cr0, eax

    dbg 'J'

    ; GDT
    lgdt [gdt64_pointer]

    dbg 'K'

    jmp 0x08:_start64

.no_long_mode:
    dbg '!'
    hlt
    jmp .no_long_mode

; ====== 64-bit Entry ======
bits 64
section .text

_start64:
    mov dx, 0xE9
    mov al, '1'
    out dx, al

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    mov dx, 0xE9
    mov al, '2'
    out dx, al

    mov rsp, stack_top

    mov dx, 0xE9
    mov al, '3'
    out dx, al

    mov edi, dword [mb2_magic32]
    mov esi, dword [mb2_info32]

    mov dx, 0xE9
    mov al, '4'
    out dx, al

    call kernel_main64

.hang64:
    cli
    hlt
    jmp .hang64

; ====== Data ======
section .data
align 4

mb2_magic32: dd 0
mb2_info32:  dd 0

align 4
gdt64:
    dq 0
gdt64_code:
    dq (1 << 43) | (1 << 44) | (1 << 47) | (1 << 53)
gdt64_data:
    dq (1 << 44) | (1 << 47) | (1 << 41)

gdt64_pointer:
    dw $ - gdt64 - 1
    dq gdt64

; ====== BSS ======
section .bss
    align 16
stack_bottom:
    resb 65536
stack_top:
