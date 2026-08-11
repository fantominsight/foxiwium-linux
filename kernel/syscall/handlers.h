#pragma once
#include <stdint.h>
#include "../syscall/syscall.h"
#include "../proc/process.h"
#include "../drivers/ps2_keyboard.h"
#include "../drivers/ps2_mouse.h"

namespace syscalls {

inline uint64_t sys_write(uint64_t fd, uint64_t buf, uint64_t len, uint64_t, uint64_t, uint64_t) {
    if (fd != 1 && fd != 2) return 0;
    const char* str = (const char*)buf;
    for (uint64_t i = 0; i < len; i++) {
        port::outb(0xE9, str[i]);
    }
    return len;
}

inline uint64_t sys_read(uint64_t fd, uint64_t buf, uint64_t len, uint64_t, uint64_t, uint64_t) {
    if (fd != 0) return 0;
    char* dst = (char*)buf;
    uint64_t count = 0;
    while (count < len) {
        int sc = keyboard::get_scancode();
        if (sc == -1) break;
        if (!(sc & 0x80)) {
            char c = keyboard::scancode_to_ascii(sc);
            if (c && c != '\b') {
                dst[count++] = c;
            }
        }
    }
    return count;
}

inline uint64_t sys_getpid(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t) {
    Process* p = proc::get_current();
    return p ? p->pid : 0;
}

inline uint64_t sys_exit(uint64_t code, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t) {
    proc::exit_process((int)code);
    return 0;
}

inline uint64_t sys_fork(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t) {
    Process* parent = proc::get_current();
    if (!parent) return (uint64_t)-1;

    int child_pid = proc::create_process(parent->name, parent->rip, true);
    if (child_pid < 0) return (uint64_t)-1;

    Process* child = &pcb.processes[child_pid - 1];
    __builtin_memcpy(child->user_stack, parent->user_stack, 4096);

    return child_pid;
}

inline uint64_t sys_gettime(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t) {
    return 0;
}

inline uint64_t sys_sleep(uint64_t ms, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t) {
    Process* p = proc::get_current();
    if (p) {
        p->state = PROC_WAITING;
        p->wait_target = ms;
        proc::schedule();
    }
    return 0;
}

inline void init() {
    syscall::register_handler(SYSCALL_WRITE, sys_write);
    syscall::register_handler(SYSCALL_READ, sys_read);
    syscall::register_handler(SYSCALL_GETPID, sys_getpid);
    syscall::register_handler(SYSCALL_EXIT, sys_exit);
    syscall::register_handler(SYSCALL_FORK, sys_fork);
    syscall::register_handler(SYSCALL_GETTIME, sys_gettime);
    syscall::register_handler(SYSCALL_SLEEP, sys_sleep);
}

}
