#pragma once
#include <stdint.h>
#include "pic.h"
#include "../port.h"

namespace idt {

struct InterruptFrame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rdi, rsi, rbp, rdx, rcx, rbx, rax;
    uint64_t int_no, err_code;
    uint64_t rip, cs, rflags, rsp, ss;
};

typedef void (*ISRHandler)(InterruptFrame*);

struct IDTEntry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed));

struct IDTPointer {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static IDTEntry idt[256];
static IDTPointer idtr;
static ISRHandler handlers[256];

extern "C" void* isr_stub_table[];

inline void set_handler(uint8_t vector, ISRHandler handler) {
    handlers[vector] = handler;
}

inline void init() {
    idtr.limit = sizeof(idt) - 1;
    idtr.base = (uint64_t)&idt;

    pic::remap();
    pic::disable_all();

    for (int i = 0; i < 256; i++) {
        idt[i] = {};
        idt[i].selector = 0x08;
        idt[i].ist = 0;
        idt[i].type_attr = 0x8E; // present, ring 0, interrupt gate
        handlers[i] = nullptr;
    }

    // Set ISR stubs
    uint64_t* stub = (uint64_t*)isr_stub_table;
    for (int i = 0; i < 256; i++) {
        uint64_t offset = stub[i];
        idt[i].offset_low  = offset & 0xFFFF;
        idt[i].offset_mid  = (offset >> 16) & 0xFFFF;
        idt[i].offset_high = (offset >> 32) & 0xFFFFFFFF;
    }

    asm volatile("lidt %0" : : "m"(idtr));
    asm volatile("sti");
}

inline void dispatch(InterruptFrame* frame) {
    uint8_t vec = frame->int_no;
    if (handlers[vec]) {
        handlers[vec](frame);
    }
    pic::eoi(vec);
}

} // namespace idt
