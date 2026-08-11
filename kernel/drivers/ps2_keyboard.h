#pragma once
#include <stdint.h>

namespace keyboard {

constexpr int KEY_BUFFER_SIZE = 128;

extern bool key_states[256];
extern volatile uint8_t key_buffer[KEY_BUFFER_SIZE];
extern volatile int key_head;
extern volatile int key_tail;

extern bool shift_pressed;
extern bool ctrl_pressed;
extern bool alt_pressed;
extern bool caps_lock;

void handle_scancode(uint8_t scancode);
char scancode_to_ascii(uint8_t scancode);
int get_scancode();

} // namespace keyboard
