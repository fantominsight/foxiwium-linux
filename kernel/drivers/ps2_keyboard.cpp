#include "ps2_keyboard.h"

namespace keyboard {

bool key_states[256] = {};
volatile uint8_t key_buffer[KEY_BUFFER_SIZE] = {};
volatile int key_head = 0;
volatile int key_tail = 0;

bool shift_pressed = false;
bool ctrl_pressed = false;
bool alt_pressed = false;
bool caps_lock = false;

void handle_scancode(uint8_t scancode) {
    bool released = scancode & 0x80;
    uint8_t code = scancode & 0x7F;

    key_states[code] = !released;

    if (code == 0x2A || code == 0x36) shift_pressed = !released;
    if (code == 0x1D) ctrl_pressed = !released;
    if (code == 0x38) alt_pressed = !released;
    if (!released && code == 0x3A) caps_lock = !caps_lock;

    if (!released) {
        int next = (key_head + 1) % KEY_BUFFER_SIZE;
        if (next != key_tail) {
            key_buffer[key_head] = scancode;
            key_head = next;
        }
    }
}

char scancode_to_ascii(uint8_t scancode) {
    uint8_t code = scancode & 0x7F;

    static const char map[128] = {
        0,  27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b',
        '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
        0, 'a','s','d','f','g','h','j','k','l',';','\'','`',
        0, '\\','z','x','c','v','b','n','m',',','.','/',  0,
        '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0
    };
    static const char map_s[128] = {
        0,  27, '!','@','#','$','%','^','&','*','(',')','_','+', '\b',
        '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
        0, 'A','S','D','F','G','H','J','K','L',':','"','~',
        0, '|','Z','X','C','V','B','N','M','<','>','?',  0,
        '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0
    };

    char c = shift_pressed ? map_s[code] : map[code];
    if (caps_lock && c >= 'a' && c <= 'z') c = c - 'a' + 'A';
    else if (caps_lock && c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
    return c;
}

int get_scancode() {
    if (key_head == key_tail) return -1;
    uint8_t sc = key_buffer[key_tail];
    key_tail = (key_tail + 1) % KEY_BUFFER_SIZE;
    return sc;
}

} // namespace keyboard
