#pragma once
#include <stdint.h>

struct TSS {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb_offset;
} __attribute__((packed));

struct GDTEntry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));

struct GDTPtr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

namespace gdt {

static GDTEntry entries[11];
static GDTPtr gdtr;
static TSS tss;

enum GDTIndex : uint16_t {
    NULL_SEL    = 0,
    KERNEL_CODE = 1,
    KERNEL_DATA = 2,
    USER_NULL   = 3,
    USER_DATA   = 4,   // selector 0x20 (SS на sysret)
    USER_CODE   = 5,   // selector 0x28 (CS на sysret; iretq: 0x2B)
    TSS_LOW     = 6,
    TSS_HIGH    = 7,
};

inline void set_entry(int index, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    entries[index].base_low    = base & 0xFFFF;
    entries[index].base_mid    = (base >> 16) & 0xFF;
    entries[index].base_high   = (base >> 24) & 0xFF;
    entries[index].limit_low   = limit & 0xFFFF;
    entries[index].access      = access;
    entries[index].granularity  = gran;
}

inline void set_tss(uint64_t base, uint32_t limit) {
    set_entry(TSS_LOW, (uint32_t)base, limit, 0x89, 0x00);

    GDTEntry high = {};
    high.base_low = (uint16_t)((base >> 32) & 0xFFFF);
    high.base_mid = (uint8_t)((base >> 48) & 0xFF);
    high.access   = (uint8_t)((base >> 56) & 0xFF);
    entries[TSS_HIGH] = high;
}

inline void init(uint64_t kernel_stack) {
    set_entry(NULL_SEL,   0, 0, 0, 0);
    set_entry(KERNEL_CODE, 0, 0xFFFFF, 0x9A, 0xA0);
    set_entry(KERNEL_DATA, 0, 0xFFFFF, 0x92, 0xC0);
    set_entry(USER_NULL,   0, 0, 0, 0);
    set_entry(USER_DATA,   0, 0xFFFFF, 0xF2, 0xC0);
    set_entry(USER_CODE,   0, 0xFFFFF, 0xFA, 0xA0);

    __builtin_memset(&tss, 0, sizeof(TSS));
    tss.rsp0 = kernel_stack;
    tss.ist1 = kernel_stack;
    tss.iopb_offset = sizeof(TSS);

    set_tss((uint64_t)&tss, sizeof(TSS) - 1);

    gdtr.limit = sizeof(entries) - 1;
    gdtr.base = (uint64_t)&entries;

    asm volatile("lgdt %0" :: "m"(gdtr) : "memory");

    asm volatile(
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "mov %%ax, %%ss\n"
        ::: "ax", "memory"
    );

    uint16_t tss_sel = TSS_LOW * 8;
    asm volatile("ltr %0" :: "r"(tss_sel));
}

inline void set_kernel_stack(uint64_t stack) {
    tss.rsp0 = stack;
}

}
