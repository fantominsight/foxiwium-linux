#pragma once
#include <stdint.h>
#include "framebuffer.h"

struct Color {
    uint8_t r, g, b, a;

    constexpr Color() : r(0), g(0), b(0), a(255) {}
    constexpr Color(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
        : r(r), g(g), b(b), a(a) {}

    uint32_t to_u32() const {
        return (a << 24) | (r << 16) | (g << 8) | b; // ARGB for framebuffer
    }

    static Color from_u32(uint32_t c) {
        return { (uint8_t)((c >> 16) & 0xFF), (uint8_t)((c >> 8) & 0xFF),
                 (uint8_t)(c & 0xFF), (uint8_t)((c >> 24) & 0xFF) };
    }

    Color blend(Color fg) const {
        if (fg.a == 255) return fg;
        if (fg.a == 0) return *this;
        uint8_t inv = 255 - fg.a;
        return {
            (uint8_t)((fg.r * fg.a + r * inv) / 255),
            (uint8_t)((fg.g * fg.a + g * inv) / 255),
            (uint8_t)((fg.b * fg.a + b * inv) / 255), 255
        };
    }
};

// Common colors
namespace colors {
    constexpr Color Black       {0, 0, 0};
    constexpr Color White       {255, 255, 255};
    constexpr Color Red         {220, 50, 47};
    constexpr Color Green       {152, 195, 121};
    constexpr Color Blue        {97, 175, 239};
    constexpr Color Yellow      {229, 192, 123};
    constexpr Color Cyan        {86, 182, 194};
    constexpr Color Magenta     {198, 120, 221};
    constexpr Color Orange      {209, 154, 102};
    constexpr Color Gray        {128, 128, 128};
    constexpr Color DarkGray    {60, 60, 60};
    constexpr Color LightGray   {190, 190, 190};

    // Foxiwium theme
    constexpr Color Background  {26, 26, 46};      // #1A1A2E
    constexpr Color Panel       {40, 42, 58};      // #282A3A
    constexpr Color Accent      {139, 92, 246};    // #8B5CF6 (purple)
    constexpr Color AccentHover {167, 139, 250};   // #A78BFA
    constexpr Color Text        {240, 240, 240};   // #F0F0F0
    constexpr Color TextDim     {140, 140, 160};   // #8C8CA0
    constexpr Color WindowBg    {30, 30, 50};      // #1E1E32
    constexpr Color WindowTitle {139, 92, 246};    // #8B5CF6
    constexpr Color WindowBorder{60, 60, 80};      // #3C3C50
    constexpr Color ButtonBg    {50, 52, 70};      // #323446
    constexpr Color ButtonHover {70, 72, 90};      // #46485A
    constexpr Color ButtonPress {100, 102, 120};   // #646678
    constexpr Color Taskbar     {20, 20, 36};      // #141424
}

namespace graphics {

inline void put_pixel(uint32_t x, uint32_t y, Color c) {
    framebuffer::put_pixel(x, y, c.to_u32());
}

inline void fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, Color c) {
    uint32_t* buf = framebuffer::get_buffer();
    uint32_t fpitch = framebuffer::get_pitch() / 4;
    uint32_t color = c.to_u32();

    for (uint32_t row = y; row < y + h && row < framebuffer::get_height(); row++) {
        uint32_t offset = row * fpitch + x;
        for (uint32_t col = 0; col < w && (x + col) < framebuffer::get_width(); col++) {
            buf[offset + col] = color;
        }
    }
}

inline void draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, Color c, uint32_t thickness = 1) {
    fill_rect(x, y, w, thickness, c);                   // top
    fill_rect(x, y + h - thickness, w, thickness, c);   // bottom
    fill_rect(x, y, thickness, h, c);                   // left
    fill_rect(x + w - thickness, y, thickness, h, c);   // right
}

inline void draw_line(int x0, int y0, int x1, int y1, Color c) {
    int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int dy = (y1 > y0) ? (y1 - y0) : (y0 - y1);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    while (true) {
        put_pixel(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

inline void draw_circle(int cx, int cy, int r, Color c) {
    int x = r, y = 0;
    int d = 1 - r;
    while (x >= y) {
        put_pixel(cx+x, cy+y, c);
        put_pixel(cx-x, cy+y, c);
        put_pixel(cx+x, cy-y, c);
        put_pixel(cx-x, cy-y, c);
        put_pixel(cx+y, cy+x, c);
        put_pixel(cx-y, cy+x, c);
        put_pixel(cx+y, cy-x, c);
        put_pixel(cx-y, cy-x, c);
        y++;
        if (d <= 0) { d += 2*y + 1; }
        else { x--; d += 2*(y-x) + 1; }
    }
}

inline void fill_circle(int cx, int cy, int r, Color c) {
    for (int y = -r; y <= r; y++) {
        for (int x = -r; x <= r; x++) {
            if (x*x + y*y <= r*r) {
                put_pixel(cx + x, cy + y, c);
            }
        }
    }
}

inline void fill_triangle(int x0, int y0, int x1, int y1, int x2, int y2, Color c) {
    // Bounding box
    int min_x = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2);
    int max_x = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
    int min_y = y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2);
    int max_y = y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2);

    auto edge = [](int ax, int ay, int bx, int by, int cx, int cy) -> int {
        return (cx - ax) * (by - ay) - (cy - ay) * (bx - ax);
    };

    int area = edge(x0, y0, x1, y1, x2, y2);
    if (area == 0) return;
    bool sign = area > 0;

    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            bool b0 = (edge(x0, y0, x1, y1, x, y) >= 0) == sign;
            bool b1 = (edge(x1, y1, x2, y2, x, y) >= 0) == sign;
            bool b2 = (edge(x2, y2, x0, y0, x, y) >= 0) == sign;
            if (b0 && b1 && b2) {
                put_pixel(x, y, c);
            }
        }
    }
}

// Rounded rectangle
inline void fill_rounded_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t r, Color c) {
    fill_rect(x + r, y, w - 2*r, h, c);
    fill_rect(x, y + r, r, h - 2*r, c);
    fill_rect(x + w - r, y + r, r, h - 2*r, c);
    // Corners
    for (int cy = 0; cy < (int)r; cy++) {
        for (int cx = 0; cx < (int)r; cx++) {
            int dx = cx - (int)r;
            int dy = cy - (int)r;
            // Top-left
            if (dx*dx + dy*dy <= (int)(r*r)) {
                put_pixel(x + cx, y + cy, c);
                put_pixel(x + w - 1 - cx, y + cy, c);
                put_pixel(x + cx, y + h - 1 - cy, c);
                put_pixel(x + w - 1 - cx, y + h - 1 - cy, c);
            }
        }
    }
}

// ==================== LIQUID GLASS (alpha-blended over back buffer) ====================

// Blend a single existing back-buffer pixel with a translucent color
inline void blend_pixel(uint32_t x, uint32_t y, Color c) {
    if (x >= framebuffer::get_width() || y >= framebuffer::get_height()) return;
    uint32_t* buf = framebuffer::get_buffer();
    uint32_t idx = y * (framebuffer::get_pitch() / 4) + x;
    buf[idx] = Color::from_u32(buf[idx]).blend(c).to_u32();
}

// Semi-transparent rectangle: each pixel is blended with the content behind it
inline void fill_rect_alpha(uint32_t x, uint32_t y, uint32_t w, uint32_t h, Color c) {
    if (w == 0 || h == 0) return;
    uint32_t fw = framebuffer::get_width();
    uint32_t fh = framebuffer::get_height();
    uint32_t* buf = framebuffer::get_buffer();
    uint32_t fpitch = framebuffer::get_pitch() / 4;

    for (uint32_t row = y; row < y + h && row < fh; row++) {
        uint32_t off = row * fpitch;
        for (uint32_t col = 0; col < w && (x + col) < fw; col++) {
            uint32_t idx = off + x + col;
            buf[idx] = Color::from_u32(buf[idx]).blend(c).to_u32();
        }
    }
}

// Semi-transparent rounded rectangle; corner pixels stay transparent (true glass)
inline void fill_rounded_rect_alpha(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t r, Color c) {
    if (r == 0 || 2*r >= w || 2*r >= h) { fill_rect_alpha(x, y, w, h, c); return; }

    fill_rect_alpha(x + r, y, w - 2*r, h, c);
    fill_rect_alpha(x, y + r, r, h - 2*r, c);
    fill_rect_alpha(x + w - r, y + r, r, h - 2*r, c);
    for (int cy = 0; cy < (int)r; cy++) {
        for (int cx = 0; cx < (int)r; cx++) {
            int dx = cx - (int)r;
            int dy = cy - (int)r;
            if (dx*dx + dy*dy <= (int)(r*r)) {
                blend_pixel(x + cx, y + cy, c);
                blend_pixel(x + w - 1 - cx, y + cy, c);
                blend_pixel(x + cx, y + h - 1 - cy, c);
                blend_pixel(x + w - 1 - cx, y + h - 1 - cy, c);
            }
        }
    }
}

// Semi-transparent rectangle with only the top corners rounded (panels, title bars)
inline void fill_rounded_rect_top_alpha(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t r, Color c) {
    if (r == 0 || 2*r >= w || r >= h) { fill_rect_alpha(x, y, w, h, c); return; }

    fill_rect_alpha(x + r, y, w - 2*r, h, c);
    fill_rect_alpha(x, y + r, w, h - r, c);
    for (int cy = 0; cy < (int)r; cy++) {
        for (int cx = 0; cx < (int)r; cx++) {
            int dx = cx - (int)r;
            int dy = cy - (int)r;
            if (dx*dx + dy*dy <= (int)(r*r)) {
                blend_pixel(x + cx, y + cy, c);
                blend_pixel(x + w - 1 - cx, y + cy, c);
            }
        }
    }
}

// Translucent rounded outline
inline void draw_rounded_rect_alpha(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t r, Color c) {
    if (r == 0 || 2*r >= w || 2*r >= h) {
        fill_rect_alpha(x, y, w, 1, c);
        fill_rect_alpha(x, y + h - 1, w, 1, c);
        fill_rect_alpha(x, y, 1, h, c);
        fill_rect_alpha(x + w - 1, y, 1, h, c);
        return;
    }

    fill_rect_alpha(x + r, y, w - 2*r, 1, c);
    fill_rect_alpha(x + r, y + h - 1, w - 2*r, 1, c);
    fill_rect_alpha(x, y + r, 1, h - 2*r, c);
    fill_rect_alpha(x + w - 1, y + r, 1, h - 2*r, c);
    int r2 = (int)r * (int)r;
    int rlo = r2 - 2 * (int)r;
    for (int cy = 0; cy < (int)r; cy++) {
        for (int cx = 0; cx < (int)r; cx++) {
            int dx = cx - (int)r;
            int dy = cy - (int)r;
            int d2 = dx*dx + dy*dy;
            if (d2 <= r2 && d2 >= rlo) {
                blend_pixel(x + cx, y + cy, c);
                blend_pixel(x + w - 1 - cx, y + cy, c);
                blend_pixel(x + cx, y + h - 1 - cy, c);
                blend_pixel(x + w - 1 - cx, y + h - 1 - cy, c);
            }
        }
    }
}

} // namespace graphics
