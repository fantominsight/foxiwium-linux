#include "graphics/framebuffer.h"

namespace framebuffer {

uint32_t* real_addr = nullptr;
uint32_t  width = 0;
uint32_t  height = 0;
uint32_t  pitch = 0;
uint8_t   bpp = 0;

uint32_t back_buf[2048 * 1080];
uint32_t* back = back_buf;

void init(Multiboot2Info* info) {
    uint8_t* ptr = (uint8_t*)info + 8;

    while (true) {
        Multiboot2Tag* tag = (Multiboot2Tag*)ptr;
        if (tag->type == MB2_TAG_END) break;

        if (tag->type == MB2_TAG_FRAMEBUFFER) {
            Multiboot2TagFramebuffer* fb = (Multiboot2TagFramebuffer*)tag;
            real_addr = (uint32_t*)(uint64_t)fb->addr;
            width  = fb->width;
            height = fb->height;
            pitch  = fb->pitch;
            bpp    = fb->bpp;
        }

        uint32_t sz = tag->size;
        if (sz < 8) sz = 8;
        ptr += (sz + 7) & ~7;
    }

    back = back_buf;
}

} // namespace framebuffer
