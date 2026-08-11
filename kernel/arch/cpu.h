#pragma once
#include <stdint.h>

struct CPUData {
    uint64_t reserved;        // offset 0x00
    uint64_t kernel_stack;    // offset 0x08 — loaded into RSP on syscall
    uint64_t user_stack;      // offset 0x10 — user RSP saved here
    uint64_t current_process; // offset 0x18
    uint64_t interrupt_stack; // offset 0x20
} __attribute__((packed));

static CPUData cpu_data;

inline CPUData* get_cpu_data() { return &cpu_data; }
