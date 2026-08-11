#pragma once
#include <stdint.h>
#include "port.h"

namespace power {

// ACPI S5 power-off. QEMU (i440fx/q35) exposes PM1a_CNT at 0x604 with the
// default PM base of 0x600. SLP_TYP=5 (S5) | SLP_EN => request power off.
inline void shutdown() {
    asm volatile("cli");
    // QEMU accepts either the full S5 value or just SLP_EN.
    port::outw(0x604, 0x3400);
    port::outw(0x604, 0x2000);
    for (;;) asm volatile("hlt");
}

// Keyboard controller CPU reset (works on QEMU and real hardware).
inline void reboot() {
    asm volatile("cli");
    for (int i = 0; i < 0x10000; i++) {
        if ((port::inb(0x64) & 0x02) == 0) break;   // wait for input buffer empty
    }
    port::outb(0x64, 0xFE);
    for (;;) asm volatile("hlt");
}

} // namespace power
