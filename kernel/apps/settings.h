#pragma once
#include <stdint.h>
#include "../gui/window.h"
#include "../graphics/graphics.h"
#include "../graphics/font.h"
#include "../mm/pmm.h"
#include "../drivers/pit.h"

namespace settings {

constexpr int ITEM_H = 36;
constexpr int SECTION_H = 28;
constexpr int MARGIN = 16;

struct SettingsState {
    int selected_section;
    int selected_item;
    int scroll_y;
    bool theme_changed;
    int accent_r, accent_g, accent_b;
    int brightness;
    bool show_animations;
    bool show_cursor_trail;
    int cursor_size;
};

static SettingsState sstate;

inline void init() {
    sstate.selected_section = 0;
    sstate.selected_item = 0;
    sstate.scroll_y = 0;
    sstate.theme_changed = false;
    sstate.accent_r = 139;
    sstate.accent_g = 92;
    sstate.accent_b = 246;
    sstate.brightness = 100;
    sstate.show_animations = true;
    sstate.show_cursor_trail = true;
    sstate.cursor_size = 14;
}

struct SettingItem {
    const char* label;
    const char* value;
    bool is_toggle;
    bool* toggle_ptr;
};

struct SettingSection {
    const char* title;
    SettingItem items[8];
    int item_count;
};

static SettingSection sections[] = {
    {
        "Appearance", {
            {"Theme", "Dark", false, nullptr},
            {"Accent Color", "Purple", false, nullptr},
            {"Wallpaper", "Gradient", false, nullptr},
            {"Font Size", "Normal", false, nullptr},
            {"Animations", "", true, &sstate.show_animations},
        }, 5
    },
    {
        "Display", {
            {"Resolution", "1920x1080", false, nullptr},
            {"Refresh Rate", "60 Hz", false, nullptr},
            {"Brightness", "", false, nullptr},
            {"Night Mode", "", true, nullptr},
        }, 4
    },
    {
        "Input", {
            {"Cursor Size", "Medium", false, nullptr},
            {"Cursor Trail", "", true, &sstate.show_cursor_trail},
            {"Double-click Speed", "Normal", false, nullptr},
            {"Keyboard Layout", "US QWERTY", false, nullptr},
        }, 4
    },
    {
        "System", {
            {"Hostname", "foxiwium", false, nullptr},
            {"Timezone", "UTC", false, nullptr},
            {"Auto-login", "", true, nullptr},
            {"Sound", "", true, nullptr},
        }, 4
    },
    {
        "About", {
            {"OS Name", "Foxiwium OS", false, nullptr},
            {"Version", "v1.7.3", false, nullptr},
            {"Kernel", "0.0-1-generic", false, nullptr},
            {"Architecture", "x86_64", false, nullptr},
            {"License", "MIT", false, nullptr},
        }, 5
    },
};

static constexpr int section_count = 5;

inline void draw(gui::Window* w) {
    int x = w->bounds.x + 1;
    int y = w->bounds.y + 30;
    int ww = w->bounds.w - 2;
    int wh = w->bounds.h - 30;

    graphics::fill_rect(x, y, ww, wh, Color(22, 22, 32));

    // Sidebar
    int sbw = 160;
    graphics::fill_rect(x, y, sbw, wh, Color(16, 16, 24));
    graphics::fill_rect(x + sbw, y, 1, wh, Color(40, 40, 55));

    font::draw_string(x + MARGIN, y + 12, "Settings", Color(240, 240, 240), 1);
    graphics::fill_rect(x + MARGIN, y + 30, sbw - MARGIN * 2, 1, Color(40, 40, 55));

    for (int i = 0; i < section_count; i++) {
        int sy = y + 40 + i * 28;
        bool sel = (i == sstate.selected_section);
        if (sel) {
            graphics::fill_rounded_rect(x + 4, sy - 2, sbw - 8, 24, 6, Color(139, 92, 246));
        }
        font::draw_string(x + MARGIN, sy + 2, sections[i].title,
                          sel ? Color(240, 240, 240) : Color(140, 140, 160));
    }

    // Content area
    int cx = x + sbw + 16;
    int cy = y + 12;
    int cw = ww - sbw - 32;

    SettingSection& sec = sections[sstate.selected_section];
    font::draw_string(cx, cy, sec.title, Color(240, 240, 240));
    cy += 24;
    graphics::fill_rect(cx, cy, cw, 1, Color(40, 40, 55));
    cy += 12;

    for (int i = 0; i < sec.item_count; i++) {
        int iy = cy + i * ITEM_H;
        bool sel = (i == sstate.selected_item);

        if (sel) {
            graphics::fill_rounded_rect(cx - 4, iy, cw + 8, ITEM_H - 4, 6, Color(35, 35, 50));
        }

        font::draw_string(cx + 4, iy + 8, sec.items[i].label, Color(200, 200, 210));

        if (sec.items[i].is_toggle && sec.items[i].toggle_ptr) {
            bool on = *sec.items[i].toggle_ptr;
            int toggle_x = cx + cw - 44;
            int toggle_y = iy + 6;
            graphics::fill_rounded_rect(toggle_x, toggle_y, 36, 18, 9,
                                        on ? Color(99, 201, 63) : Color(60, 60, 75));
            int knob_x = on ? toggle_x + 20 : toggle_x + 2;
            graphics::fill_circle(knob_x + 7, toggle_y + 9, 7,
                                  on ? Color(240, 240, 240) : Color(140, 140, 160));
        } else if (sec.items[i].value[0]) {
            int vw = font::text_width(sec.items[i].value);
            font::draw_string(cx + cw - vw - 8, iy + 8, sec.items[i].value, Color(140, 140, 160));
        }
    }

    // Version info at bottom
    int by = y + wh - 40;
    graphics::fill_rect(x + sbw + 1, by, ww - sbw - 1, 1, Color(40, 40, 55));
    font::draw_string(cx, by + 10, "Foxiwium OS v1.7.3", Color(80, 80, 100));
}

inline bool handle_click(gui::Window* w, int mx, int my) {
    int x = w->bounds.x + 1;
    int y = w->bounds.y + 30;
    int ww = w->bounds.w - 2;
    int sbw = 160;

    // Sidebar clicks
    if (mx >= x && mx < x + sbw) {
        for (int i = 0; i < section_count; i++) {
            int sy = y + 40 + i * 28;
            if (my >= sy && my < sy + 24) {
                sstate.selected_section = i;
                sstate.selected_item = 0;
                return true;
            }
        }
    }

    // Content area clicks
    int cx = x + sbw + 16;
    int cy = y + 12 + 24 + 12;
    int cw = ww - sbw - 32;
    SettingSection& sec = sections[sstate.selected_section];

    for (int i = 0; i < sec.item_count; i++) {
        int iy = cy + i * ITEM_H;
        if (mx >= cx - 4 && mx < cx + cw + 4 && my >= iy && my < iy + ITEM_H - 4) {
            sstate.selected_item = i;

            // Toggle click
            if (sec.items[i].is_toggle && sec.items[i].toggle_ptr) {
                int toggle_x = cx + cw - 44;
                if (mx >= toggle_x && mx < toggle_x + 36) {
                    *sec.items[i].toggle_ptr = !*sec.items[i].toggle_ptr;
                    return true;
                }
            }
            return true;
        }
    }
    return false;
}

inline bool handle_hover(gui::Window* w, int mx, int my) {
    (void)w; (void)mx; (void)my;
    return false;
}

}
