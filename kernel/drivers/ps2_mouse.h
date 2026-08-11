#pragma once
#include <stdint.h>

namespace mouse {

struct MousePacket {
    int16_t x;
    int16_t y;
    bool left;
    bool right;
    bool middle;
    int8_t scroll;
};

extern uint8_t mouse_buffer[4];
extern int mouse_cycle;
extern MousePacket current;

extern volatile int cursor_x;
extern volatile int cursor_y;

extern volatile bool left_pressed;
extern volatile bool right_pressed;
extern volatile bool left_pressed_prev;
extern volatile int scroll_delta;

void handle_packet();
MousePacket get_state();
void init();

} // namespace mouse
