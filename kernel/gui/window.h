#pragma once
#include <stdint.h>
#include <stddef.h>
#include "../graphics/graphics.h"
#include "../graphics/font.h"

namespace gui {

constexpr int MAX_WINDOWS = 32;

struct Rect {
    int x, y, w, h;
    bool contains(int px, int py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
};

// Widget base
struct Widget {
    Rect bounds;
    bool visible = true;
    bool enabled = true;
    Color bg = colors::ButtonBg;
    Color fg = colors::Text;
    Widget* parent_widget = nullptr;
    virtual ~Widget() {}
    virtual void draw() = 0;
    virtual bool handle_click(int, int) { return false; }
    virtual bool handle_hover(int, int) { return false; }
};

// Label
struct Label : Widget {
    const char* text = "";
    int font_scale = 1;
    Label() { bg = colors::WindowBg; }
    void draw() override;
};

// Button
struct Button : Widget {
    const char* text = "";
    bool pressed = false;
    bool hovered = false;
    void (*on_click)(Button*) = nullptr;
    Button() { bg = colors::ButtonBg; fg = colors::Text; }
    void draw() override;
    bool handle_click(int x, int y) override;
    bool handle_hover(int x, int y) override;
};

// Window
struct Window {
    bool active = false;
    bool dragging = false;
    bool visible = true;
    bool focused = false;
    bool resizing = false;

    Rect bounds;
    Rect title_bar;
    char title[64] = {};
    Color title_color = colors::WindowTitle;

    // Dragging state
    int drag_off_x = 0, drag_off_y = 0;

    // Resizing state
    int resize_edge = 0;
    int resize_min_w = 200;
    int resize_min_h = 120;

    // Content area
    Rect content;

    // Widgets
    Widget* widgets[64] = {};
    int widget_count = 0;

    // Content rendering callback
    void (*on_draw)(Window*) = nullptr;
    void (*on_close)(Window*) = nullptr;

    static constexpr int TITLE_HEIGHT = 32;
    static constexpr int BORDER = 2;
    static constexpr int CORNER_RADIUS = 8;

    void init(int x, int y, int w, int h, const char* title);
    void draw();
    bool handle_click(int mx, int my);
    void add_widget(Widget* w);
    void close();
};

// Desktop
struct Desktop {
    Window* windows[MAX_WINDOWS] = {};
    int window_count = 0;
    int active_window = -1;
    Window* hovered_window = nullptr;

    // Mouse state
    int mouse_x = 960, mouse_y = 540;
    bool mouse_left = false;
    bool mouse_right = false;

    // Cursor
    static constexpr int CURSOR_SIZE = 16;

    void init();
    void draw();
    Window* create_window(int x, int y, int w, int h, const char* title);
    void close_window(int idx);
    void handle_mouse(int x, int y, bool left, bool right);
    void draw_cursor();
    void draw_taskbar();
    void draw_wallpaper();
};

} // namespace gui
