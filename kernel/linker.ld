/* Foxiwium OS - Linker Script for x86_64 */
ENTRY(_start32)

SECTIONS
{
    /* Load at 1MB (standard for Multiboot kernels) */
    . = 1M;

    /* Multiboot2 header must be in the first 32KB */
    .multiboot2 ALIGN(8) :
    {
        *(.multiboot2)
    }

    /* Code section */
    .text ALIGN(4K) :
    {
        *(.text)
        *(.text.*)
    }

    /* Read-only data */
    .rodata ALIGN(4K) :
    {
        *(.rodata)
        *(.rodata.*)
    }

    /* Initialized data */
    .data ALIGN(4K) :
    {
        *(.data)
        *(.data.*)
    }

    /* Uninitialized data (BSS) */
    .bss ALIGN(4K) :
    {
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        __bss_end = .;
    }

    /* End of kernel */
    __kernel_end = .;

    /* Discard debug and other sections */
    /DISCARD/ :
    {
        *(.eh_frame)
        *(.comment)
        *(.note*)
        *(.gnu.*)
    }
}
