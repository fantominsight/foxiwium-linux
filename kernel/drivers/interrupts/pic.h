#pragma once
#include <stdint.h>
#include "../port.h"

namespace pic {

constexpr uint16_t PIC1_CMD  = 0x20;
constexpr uint16_t PIC1_DATA = 0x21;
constexpr uint16_t PIC2_CMD  = 0xA0;
constexpr uint16_t PIC2_DATA = 0xA1;

constexpr uint8_t PIC_EOI = 0x20;

inline void remap() {
    port::outb(PIC1_CMD,  0x11); port::io_wait();
    port::outb(PIC2_CMD,  0x11); port::io_wait();
    port::outb(PIC1_DATA, 0x20); port::io_wait();
    port::outb(PIC2_DATA, 0x28); port::io_wait();
    port::outb(PIC1_DATA, 0x04); port::io_wait();
    port::outb(PIC2_DATA, 0x02); port::io_wait();
    port::outb(PIC1_DATA, 0x01); port::io_wait();
    port::outb(PIC2_DATA, 0x01); port::io_wait();
    port::outb(PIC1_DATA, 0x00); port::io_wait();
    port::outb(PIC2_DATA, 0x00); port::io_wait();
}

inline void eoi(uint8_t irq) {
    if (irq >= 8) {
        port::outb(PIC2_CMD, PIC_EOI);
    }
    port::outb(PIC1_CMD, PIC_EOI);
}

inline void mask_irq(uint8_t irq) {
    uint16_t port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    uint8_t shift = irq % 8;
    uint8_t val = port::inb(port) | (1 << shift);
    port::outb(port, val);
}

inline void unmask_irq(uint8_t irq) {
    uint16_t port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    uint8_t shift = irq % 8;
    uint8_t val = port::inb(port) & ~(1 << shift);
    port::outb(port, val);
}

inline void disable_all() {
    port::outb(PIC1_DATA, 0xFF);
    port::outb(PIC2_DATA, 0xFF);
}

} // namespace pic
