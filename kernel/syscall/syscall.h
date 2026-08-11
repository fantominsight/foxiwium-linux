#pragma once
#include <stdint.h>

#define MSR_STAR        0xC0000081
#define MSR_LSTAR       0xC0000082
#define MSR_FMASK       0xC0000084
#define MSR_EFER        0xC0000080
#define MSR_GS_BASE     0xC0000101
#define MSR_KERNEL_GS_BASE 0xC0000102

#define SYSCALL_READ    0
#define SYSCALL_WRITE   1
#define SYSCALL_OPEN    2
#define SYSCALL_CLOSE   3
#define SYSCALL_FORK    4
#define SYSCALL_EXEC    5
#define SYSCALL_EXIT    6
#define SYSCALL_WAIT    7
#define SYSCALL_GETPID  8
#define SYSCALL_SBRK    9
#define SYSCALL_MMAP    10
#define SYSCALL_GETTIME 11
#define SYSCALL_SLEEP   12

#define USER_CS 0x2B
#define USER_SS 0x23
#define USER_RSP 0x7FFFFFFFE000ULL

typedef uint64_t (*syscall_handler_t)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

namespace syscall {

static syscall_handler_t handlers[16] = {};

inline void write_msr(uint32_t msr, uint64_t val) {
    asm volatile(
        "wrmsr"
        :: "a"((uint32_t)val), "d"((uint32_t)(val >> 32)), "c"(msr)
        : "memory"
    );
}

inline uint64_t read_msr(uint32_t msr) {
    uint32_t lo, hi;
    asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

extern "C" uint64_t syscall_entry_handler(uint64_t rdi, uint64_t rsi, uint64_t rdx,
                                           uint64_t r10, uint64_t r8, uint64_t r9,
                                           uint64_t syscall_num);

inline void register_handler(uint64_t num, syscall_handler_t handler) {
    if (num < 16) handlers[num] = handler;
}

inline uint64_t dispatch(uint64_t rdi, uint64_t rsi, uint64_t rdx,
                          uint64_t r10, uint64_t r8, uint64_t r9,
                          uint64_t syscall_num) {
    if (syscall_num >= 16 || !handlers[syscall_num]) return (uint64_t)-1;
    return handlers[syscall_num](rdi, rsi, rdx, r10, r8, r9);
}

}
