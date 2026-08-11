#pragma once
#include <stdint.h>
#include "../gui/window.h"
#include "../graphics/graphics.h"
#include "../graphics/font.h"
#include "../mm/pmm.h"
#include "../drivers/pit.h"

namespace about {

inline void draw(gui::Window* w) {
    int x = w->bounds.x + 1;
    int y = w->bounds.y + 30;
    int ww = w->bounds.w - 2;
    int wh = w->bounds.h - 30;

    graphics::fill_rect(x, y, ww, wh, Color(22, 22, 32));

    int cx = x + ww / 2;
    int cy = y + 20;

    // Fox logo
    int fcx = cx;
    int fcy = cy + 50;

    // Fox ears
    graphics::fill_triangle(fcx - 50, fcy - 30, fcx - 25, fcy - 70, fcx - 5, fcy - 30, Color(209, 154, 102));
    graphics::fill_triangle(fcx + 50, fcy - 30, fcx + 25, fcy - 70, fcx + 5, fcy - 30, Color(209, 154, 102));

    // Fox face
    graphics::fill_circle(fcx, fcy, 45, Color(209, 154, 102));
    graphics::fill_circle(fcx, fcy + 8, 38, Color(240, 240, 240));

    // Eyes
    graphics::fill_circle(fcx - 14, fcy - 6, 5, Color(30, 30, 40));
    graphics::fill_circle(fcx + 14, fcy - 6, 5, Color(30, 30, 40));
    graphics::fill_circle(fcx - 12, fcy - 8, 2, Color(255, 255, 255));
    graphics::fill_circle(fcx + 16, fcy - 8, 2, Color(255, 255, 255));

    // Nose
    graphics::fill_circle(fcx, fcy + 8, 4, Color(30, 30, 40));

    // Text
    int ty = fcy + 60;
    font::draw_string_centered(cx, ty, "Foxiwium OS", Color(240, 240, 240), 2);
    ty += 28;
    font::draw_string_centered(cx, ty, "Version 1.7.3", Color(139, 92, 246));
    ty += 22;
    font::draw_string_centered(cx, ty, "Build 2026.07.17", Color(100, 100, 120));
    ty += 30;

    graphics::fill_rect(x + 40, ty, ww - 80, 1, Color(40, 40, 55));
    ty += 12;

    // System info
    struct InfoLine { const char* key; const char* value; };
    InfoLine info[] = {
        {"Kernel",    "Foxiwium Kernel 0.0-1-generic"},
        {"Arch",      "x86_64 (long mode)"},
        {"Boot",      "Multiboot2 / GRUB"},
        {"Memory",    ""},
        {"Display",   "1920x1080x32bpp"},
        {"Shell",     "fox-sh (embedded)"},
        {"Theme",     "FramePerfect UI 2/3"},
        {"License",   "MIT"},
    };

    int info_count = 8;
    // Fill in memory dynamically
    char mem_buf[64];
    uint64_t total = pmm::get_total_pages();
    uint64_t used = pmm::get_used_pages();
    int mi = 0;
    char tmp[16]; int ti = 0;
    int num = (int)(used * 4 / 1024);
    if (num == 0) { tmp[ti++] = '0'; }
    else { while (num > 0) { tmp[ti++] = '0' + num % 10; num /= 10; } }
    while (ti > 0) mem_buf[mi++] = tmp[--ti];
    mem_buf[mi++] = '/';
    num = (int)(total * 4 / 1024);
    ti = 0;
    if (num == 0) { tmp[ti++] = '0'; }
    else { while (num > 0) { tmp[ti++] = '0' + num % 10; num /= 10; } }
    while (ti > 0) mem_buf[mi++] = tmp[--ti];
    mem_buf[mi++] = 'M';
    mem_buf[mi++] = 'B';
    mem_buf[mi] = 0;
    info[3].value = mem_buf;

    for (int i = 0; i < info_count; i++) {
        int iw = font::text_width(info[i].key);
        font::draw_string(x + 40, ty, info[i].key, Color(139, 92, 246));
        font::draw_string(x + 40 + iw + 16, ty, info[i].value, Color(180, 180, 195));
        ty += 18;
    }

    ty += 10;
    graphics::fill_rect(x + 40, ty, ww - 80, 1, Color(40, 40, 55));
    ty += 12;
    font::draw_string_centered(cx, ty, "A hobby OS built from scratch.", Color(100, 100, 120));
    ty += 16;
    font::draw_string_centered(cx, ty, "github.com/foxiwium", Color(139, 92, 246));
}

inline bool handle_click(gui::Window* w, int mx, int my) {
    (void)w; (void)mx; (void)my;
    return false;
}

inline bool handle_hover(gui::Window* w, int mx, int my) {
    (void)w; (void)mx; (void)my;
    return false;
}

}
