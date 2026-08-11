#include "ps2_mouse.h"
#include "port.h"
#include "../graphics/framebuffer.h"

namespace mouse {

uint8_t mouse_buffer[4];
int mouse_cycle = 0;
MousePacket current = {};

volatile int cursor_x = 960;
volatile int cursor_y = 540;

volatile bool left_pressed = false;
volatile bool right_pressed = false;
volatile bool left_pressed_prev = false;
volatile int scroll_delta = 0;

static bool has_wheel = false;

static void wait_input() {
    int timeout = 100000;
    while (!(port::inb(0x64) & 0x01)) { if (--timeout == 0) return; }
}

static void wait_output() {
    int timeout = 100000;
    while (port::inb(0x64) & 0x02) { if (--timeout == 0) return; }
}

static void write(uint8_t data) {
    wait_output();
    port::outb(0x64, 0xD4);
    wait_output();
    port::outb(0x60, data);
}

static uint8_t read() {
    wait_input();
    return port::inb(0x60);
}

static void flush_buffer() {
    while (port::inb(0x64) & 0x01) {
        port::inb(0x60);
    }
}

static void process_packet() {
    int8_t dx = (int8_t)mouse_buffer[1];
    int8_t dy = -(int8_t)mouse_buffer[2];

    uint32_t fw = framebuffer::get_width();
    uint32_t fh = framebuffer::get_height();
    if (fw == 0) fw = 1920;
    if (fh == 0) fh = 1080;

    int new_x = cursor_x + dx;
    int new_y = cursor_y + dy;

    if (new_x < 0) new_x = 0;
    if (new_y < 0) new_y = 0;
    if (new_x > (int)fw - 1) new_x = fw - 1;
    if (new_y > (int)fh - 1) new_y = fh - 1;

    cursor_x = new_x;
    cursor_y = new_y;

    current.x = cursor_x;
    current.y = cursor_y;
    current.left = mouse_buffer[0] & 0x01;
    current.right = mouse_buffer[0] & 0x02;
    current.middle = mouse_buffer[0] & 0x04;

    left_pressed_prev = left_pressed;
    left_pressed = current.left;
    right_pressed = current.right;

    if (has_wheel) {
        current.scroll = (int8_t)mouse_buffer[3];
        scroll_delta += current.scroll;
    } else {
        current.scroll = 0;
    }
}

void handle_packet() {
    // Drain the PS/2 controller buffer: QEMU/KVM can deliver several bytes
    // for one IRQ, and reading a single byte per IRQ loses packets (bad
    // cursor drift, missed clicks). Process up to a bounded number of bytes.
    for (int i = 0; i < 16; i++) {
        uint8_t data = port::inb(0x60);

        if (mouse_cycle == 0) {
            if (!(data & 0x08)) {
                if (!(port::inb(0x64) & 0x01)) break;
                continue;
            }
            mouse_buffer[0] = data;
            mouse_cycle = 1;
        } else if (mouse_cycle == 1) {
            mouse_buffer[1] = data;
            mouse_cycle = 2;
        } else if (mouse_cycle == 2) {
            mouse_buffer[2] = data;
            if (has_wheel) {
                mouse_cycle = 3;
            } else {
                process_packet();
                mouse_cycle = 0;
            }
        } else if (mouse_cycle == 3) {
            if (data & 0x08) {
                has_wheel = false;
                process_packet();
                mouse_buffer[0] = data;
                mouse_cycle = 1;
            } else {
                mouse_buffer[3] = data;
                process_packet();
                mouse_cycle = 0;
            }
        }

        if (!(port::inb(0x64) & 0x01)) break;
    }
}

MousePacket get_state() {
    return current;
}

void init() {
    flush_buffer();

    wait_output();
    port::outb(0x64, 0xA8);

    port::outb(0x64, 0x20);
    wait_input();
    uint8_t status = port::inb(0x60);
    status |= (1 << 1);
    wait_output();
    port::outb(0x64, 0x60);
    wait_output();
    port::outb(0x60, status);

    flush_buffer();

    write(0xFF);
    read();

    write(0xF6);
    read();

    write(0xF3);
    read();
    write(0xC8);
    read();
    write(0xF3);
    read();
    write(0x64);
    read();
    write(0xF3);
    read();
    write(0x50);
    read();

    uint8_t id = read();
    if (id == 0x03) {
        has_wheel = true;
    } else {
        has_wheel = false;
    }

    write(0xF4);
    read();

    mouse_cycle = 0;
    cursor_x = framebuffer::get_width() / 2;
    cursor_y = framebuffer::get_height() / 2;
    if (cursor_x == 0) cursor_x = 960;
    if (cursor_y == 0) cursor_y = 540;
}

} // namespace mouse
