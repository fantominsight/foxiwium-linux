#pragma once
#include <stdint.h>
#include "port.h"
#include "interrupts/pic.h"

namespace pit {

static volatile uint64_t ticks = 0;
static uint32_t freq = 100;

inline uint64_t get_ticks() { return ticks; }

inline void handler() {
    ticks++;
}

inline void init(uint32_t hz = 100) {
    freq = hz;
    uint32_t divisor = 1193182 / hz;
    port::outb(0x43, 0x36);
    port::outb(0x40, divisor & 0xFF);
    port::outb(0x40, (divisor >> 8) & 0xFF);
    pic::unmask_irq(0);
}

inline void sleep(uint64_t ms) {
    uint64_t target = ticks + ms;
    while (ticks < target) {
        asm volatile("hlt");
    }
}

}
