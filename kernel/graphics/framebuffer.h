#pragma once
#include <stdint.h>
#include <stddef.h>

struct Multiboot2Info {
    uint32_t total_size;
    uint32_t reserved;
};

constexpr uint32_t MB2_TAG_END         = 0;
constexpr uint32_t MB2_TAG_MODULE      = 3;
constexpr uint32_t MB2_TAG_FRAMEBUFFER = 8;

struct Multiboot2Tag {
    uint32_t type;
    uint32_t size;
};

struct Multiboot2TagFramebuffer {
    Multiboot2Tag base;
    uint64_t addr;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint8_t  bpp;
    uint8_t  fb_type;
    uint8_t  reserved;
} __attribute__((packed));

struct Multiboot2TagModule {
    Multiboot2Tag base;
    uint32_t mod_start;
    uint32_t mod_end;
    char     cmdline[0];
} __attribute__((packed));

namespace framebuffer {

extern uint32_t* real_addr;
extern uint32_t  width;
extern uint32_t  height;
extern uint32_t  pitch;
extern uint8_t   bpp;

// Back buffer — draws go here, flip() copies to screen
extern uint32_t back_buf[];
extern uint32_t* back;

void init(Multiboot2Info* info);

inline void put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (x >= width || y >= height) return;
    back[y * (pitch / 4) + x] = color;
}

inline uint32_t get_pixel(uint32_t x, uint32_t y) {
    if (x >= width || y >= height) return 0;
    return back[y * (pitch / 4) + x];
}

inline void clear(uint32_t color = 0xFF1A1A2E) {
    uint32_t count = (pitch / 4) * height;
    for (uint32_t i = 0; i < count; i++) {
        back[i] = color;
    }
}

// Copy back buffer to real framebuffer (fast, single pass)
inline void flip() {
    uint32_t count = (pitch / 4) * height;
    uint32_t* dst = real_addr;
    uint32_t* src = back;
    __asm__ volatile(
        "rep movsl"
        : "+D"(dst), "+S"(src), "+c"(count)
        :
        : "memory"
    );
}

inline uint32_t* get_buffer() { return back; }
inline uint32_t get_width()   { return width; }
inline uint32_t get_height()  { return height; }
inline uint32_t get_pitch()   { return pitch; }

} // namespace framebuffer
