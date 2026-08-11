#include "window.h"

namespace gui {

// === Label ===
void Label::draw() {
    if (!visible) return;
    graphics::fill_rect(bounds.x, bounds.y, bounds.w, bounds.h, bg);
    font::draw_string(bounds.x + 4, bounds.y + 4, text, fg, font_scale);
}

// === Button ===
void Button::draw() {
    if (!visible) return;
    Color c = pressed ? colors::ButtonPress : (hovered ? colors::ButtonHover : bg);
    graphics::fill_rounded_rect(bounds.x, bounds.y, bounds.w, bounds.h, 6, c);
    if (text) {
        int tw = font::text_width(text, 1);
        int th = font::text_height(1);
        font::draw_string(
            bounds.x + (bounds.w - tw) / 2,
            bounds.y + (bounds.h - th) / 2,
            text, fg
        );
    }
}

bool Button::handle_click(int x, int y) {
    if (!visible || !enabled) return false;
    if (bounds.contains(x, y)) {
        pressed = true;
        if (on_click) on_click(this);
        return true;
    }
    return false;
}

bool Button::handle_hover(int x, int y) {
    if (!visible || !enabled) return false;
    hovered = bounds.contains(x, y);
    return hovered;
}

// === Window ===
void Window::init(int x, int y, int w, int h, const char* t) {
    bounds = {x, y, w, h};
    title_bar = {x, y, w, TITLE_HEIGHT};
    content = {x, y + TITLE_HEIGHT, w, h - TITLE_HEIGHT};
    active = true;
    visible = true;
    widget_count = 0;
    dragging = false;

    // Copy title
    int i = 0;
    while (t[i] && i < 63) { title[i] = t[i]; i++; }
    title[i] = 0;
}

void Window::draw() {
    if (!visible || !active) return;

    // Shadow
    graphics::fill_rect(bounds.x + 4, bounds.y + 4, bounds.w, bounds.h, Color(0, 0, 0, 80));

    // Window body
    graphics::fill_rounded_rect(bounds.x, bounds.y, bounds.w, bounds.h, CORNER_RADIUS, colors::WindowBg);

    // Border
    Color bc = focused ? colors::Accent : colors::WindowBorder;
    graphics::draw_rect(bounds.x, bounds.y, bounds.w, bounds.h, bc, 2);

    // Title bar
    graphics::fill_rounded_rect(bounds.x, bounds.y, bounds.w, TITLE_HEIGHT, CORNER_RADIUS, title_color);

    // Title text
    font::draw_string(bounds.x + 12, bounds.y + 8, title, colors::White);

    // Close button (X)
    int cb_x = bounds.x + bounds.w - 28;
    int cb_y = bounds.y + 6;
    graphics::fill_rounded_rect(cb_x, cb_y, 20, 20, 4, Color(220, 60, 47));
    font::draw_string(cb_x + 5, cb_y + 2, "X", colors::White);

    // Draw widgets
    for (int i = 0; i < widget_count; i++) {
        if (widgets[i]) widgets[i]->draw();
    }

    // Custom draw callback
    if (on_draw) on_draw(this);
}

bool Window::handle_click(int mx, int my) {
    if (!visible || !active) return false;

    if (!bounds.contains(mx, my)) return false;

    // Close button? (right side - KDE style)
    int cb_x = bounds.x + bounds.w - 26;
    int cb_y = bounds.y + 6;
    if (mx >= cb_x && mx < cb_x + 20 && my >= cb_y && my < cb_y + 20) {
        if (on_close) on_close(this);
        else close();
        return true;
    }

    // Title bar drag?
    if (title_bar.contains(mx, my)) {
        dragging = true;
        drag_off_x = mx - bounds.x;
        drag_off_y = my - bounds.y;
        return true;
    }

    // Edge resize? Check edges (8px border)
    int edge = 0;
    int br = 8;
    if (mx >= bounds.x && mx < bounds.x + br) edge |= 1;
    if (mx >= bounds.x + bounds.w - br && mx < bounds.x + bounds.w) edge |= 2;
    if (my >= bounds.y && my < bounds.y + br) edge |= 4;
    if (my >= bounds.y + bounds.h - br && my < bounds.y + bounds.h) edge |= 8;

    if (edge && (mx < bounds.x + bounds.w || mx >= bounds.x + bounds.w - br) &&
        (my < bounds.y + bounds.h || my >= bounds.y + bounds.h - br)) {
        resizing = true;
        resize_edge = edge;
        drag_off_x = mx - bounds.x;
        drag_off_y = my - bounds.y;
        return true;
    }

    // Widget clicks
    for (int i = widget_count - 1; i >= 0; i--) {
        if (widgets[i] && widgets[i]->handle_click(mx, my)) return true;
    }

    return true;
}

void Window::add_widget(Widget* w) {
    if (widget_count < 64) {
        widgets[widget_count++] = w;
    }
}

void Window::close() {
    active = false;
    visible = false;
}

// === Desktop ===
void Desktop::init() {
    window_count = 0;
    active_window = -1;
    mouse_x = framebuffer::get_width() / 2;
    mouse_y = framebuffer::get_height() / 2;
}

Window* Desktop::create_window(int x, int y, int w, int h, const char* title) {
    if (window_count >= MAX_WINDOWS) return nullptr;

    Window* win = new Window();
    win->init(x, y, w, h, title);

    windows[window_count] = win;
    active_window = window_count;
    window_count++;

    // Unfocus all, focus this
    for (int i = 0; i < window_count; i++) {
        if (windows[i]) windows[i]->focused = (i == active_window);
    }

    return win;
}

void Desktop::close_window(int idx) {
    if (idx < 0 || idx >= window_count) return;
    if (windows[idx]) {
        windows[idx]->close();
    }
}

void Desktop::draw_wallpaper() {
    uint32_t fw = framebuffer::get_width();
    uint32_t fh = framebuffer::get_height();

    // Gradient background
    for (uint32_t y = 0; y < fh; y++) {
        float t = (float)y / (float)fh;
        uint8_t r = (uint8_t)(26 + t * 10);
        uint8_t g = (uint8_t)(26 + t * 8);
        uint8_t b = (uint8_t)(46 + t * 20);
        Color line(r, g, b);
        graphics::fill_rect(0, y, fw, 1, line);
    }

    // Foxiwium logo in center (simple fox icon)
    int cx = fw / 2;
    int cy = fh / 2 - 40;

    // Fox ears (triangles)
    graphics::fill_triangle(cx - 80, cy - 60, cx - 40, cy - 120, cx - 10, cy - 60, colors::Orange);
    graphics::fill_triangle(cx + 80, cy - 60, cx + 40, cy - 120, cx + 10, cy - 60, colors::Orange);

    // Fox face (circle)
    graphics::fill_circle(cx, cy, 70, colors::Orange);
    graphics::fill_circle(cx, cy + 10, 60, colors::White);

    // Eyes
    graphics::fill_circle(cx - 22, cy - 10, 8, colors::Black);
    graphics::fill_circle(cx + 22, cy - 10, 8, colors::Black);
    graphics::fill_circle(cx - 20, cy - 12, 3, colors::White);
    graphics::fill_circle(cx + 24, cy - 12, 3, colors::White);

    // Nose
    graphics::fill_circle(cx, cy + 10, 6, colors::Black);

    // Text
    font::draw_string_centered(cx, cy + 90, "Foxiwium OS", colors::Text, 2);
    font::draw_string_centered(cx, cy + 130, "v0.1.0", colors::TextDim, 1);
}

void Desktop::draw_taskbar() {
    uint32_t fw = framebuffer::get_width();
    uint32_t fh = framebuffer::get_height();
    int taskbar_h = 48;

    // Taskbar background
    graphics::fill_rect(0, fh - taskbar_h, fw, taskbar_h, colors::Taskbar);
    graphics::draw_line(0, fh - taskbar_h, fw, fh - taskbar_h, colors::WindowBorder);

    // Start button
    graphics::fill_rounded_rect(8, fh - taskbar_h + 8, 100, 32, 6, colors::Accent);
    font::draw_string(24, fh - taskbar_h + 16, "Foxiwium", colors::White);

    // Window buttons
    int bx = 120;
    for (int i = 0; i < window_count; i++) {
        if (!windows[i] || !windows[i]->active) continue;
        Color c = (i == active_window) ? colors::ButtonHover : colors::ButtonBg;
        graphics::fill_rounded_rect(bx, fh - taskbar_h + 8, 120, 32, 6, c);
        font::draw_string(bx + 8, fh - taskbar_h + 16, windows[i]->title, colors::Text);
        bx += 130;
    }

    // Clock area (right side)
    // Placeholder: just show "Foxiwium"
    font::draw_string(fw - 120, fh - taskbar_h + 16, "Foxiwium", colors::TextDim);
}

void Desktop::draw_cursor() {
    // Simple arrow cursor
    int x = mouse_x;
    int y = mouse_y;
    Color c = colors::White;

    // Cursor shape (simple arrow)
    for (int i = 0; i < 16; i++) {
        for (int j = 0; j <= i && j < 12; j++) {
            if (j < 2) {
                graphics::put_pixel(x + j, y + i, c);
            }
        }
        if (i < 12) {
            graphics::put_pixel(x, y + i, c);
        }
    }
    // Outline
    for (int i = 0; i < 15; i++) {
        graphics::put_pixel(x + 1, y + i, colors::Black);
        graphics::put_pixel(x + 2, y + i + 1, colors::Black);
    }
    // Inner white
    for (int i = 0; i < 14; i++) {
        graphics::put_pixel(x, y + i, colors::White);
    }
}

void Desktop::draw() {
    draw_wallpaper();

    // Draw windows (back to front). The active window is drawn last so the
    // focused window is always on top.
    for (int i = 0; i < window_count; i++) {
        if (windows[i] && i != active_window) windows[i]->draw();
    }
    if (active_window >= 0 && active_window < window_count && windows[active_window] &&
        windows[active_window]->visible) {
        windows[active_window]->draw();
    }

    draw_taskbar();
    draw_cursor();
}

void Desktop::handle_mouse(int x, int y, bool left, bool right) {
    mouse_x = x;
    mouse_y = y;

    if (left && !mouse_left) {
        // New click. Hit-test the active window first (it is drawn on top),
        // then the rest front-to-back in creation order.
        int order[MAX_WINDOWS];
        int n = 0;
        if (active_window >= 0 && active_window < window_count) order[n++] = active_window;
        for (int i = window_count - 1; i >= 0; i--) {
            if (i != active_window) order[n++] = i;
        }
        for (int k = 0; k < n; k++) {
            int i = order[k];
            if (!windows[i] || !windows[i]->visible) continue;
            if (windows[i]->handle_click(x, y)) {
                // Focus this window
                if (active_window != i) {
                    active_window = i;
                    for (int j = 0; j < window_count; j++) {
                        if (windows[j]) windows[j]->focused = (j == i);
                    }
                }
                break;
            }
        }
    }

    // Dragging and Resizing
    if (active_window >= 0 && windows[active_window]) {
        Window* win = windows[active_window];
        if (win->dragging) {
            win->bounds.x = x - win->drag_off_x;
            win->bounds.y = y - win->drag_off_y;
            if (win->bounds.y < 0) win->bounds.y = 0;
            win->title_bar.x = win->bounds.x;
            win->title_bar.y = win->bounds.y;
            win->content.x = win->bounds.x;
            win->content.y = win->bounds.y + Window::TITLE_HEIGHT;
        }
        if (win->resizing) {
            int dx = x - (win->bounds.x + win->drag_off_x);
            int dy = y - (win->bounds.y + win->drag_off_y);
            int e = win->resize_edge;
            if (e & 1) { win->bounds.w -= dx; win->bounds.x += dx; }
            if (e & 2) { win->bounds.w += dx; }
            if (e & 4) { win->bounds.h -= dy; win->bounds.y += dy; if (win->bounds.y < 0) { win->bounds.h += (0 - win->bounds.y); win->bounds.y = 0; } }
            if (e & 8) { win->bounds.h += dy; }
            if (win->bounds.w < win->resize_min_w) { if (e & 1) win->bounds.x -= (win->resize_min_w - win->bounds.w); win->bounds.w = win->resize_min_w; }
            if (win->bounds.h < win->resize_min_h) { if (e & 4) win->bounds.y -= (win->resize_min_h - win->bounds.h); win->bounds.h = win->resize_min_h; }
            win->title_bar.x = win->bounds.x;
            win->title_bar.y = win->bounds.y;
            win->title_bar.w = win->bounds.w;
            win->content.x = win->bounds.x;
            win->content.y = win->bounds.y + Window::TITLE_HEIGHT;
            win->content.w = win->bounds.w;
            win->content.h = win->bounds.h - Window::TITLE_HEIGHT;
            win->drag_off_x = x - win->bounds.x;
            win->drag_off_y = y - win->bounds.y;
        }
    }

    if (!left && mouse_left) {
        // Release
        if (active_window >= 0 && windows[active_window]) {
            windows[active_window]->dragging = false;
            windows[active_window]->resizing = false;
        }
    }

    // Hover
    for (int i = window_count - 1; i >= 0; i--) {
        if (!windows[i] || !windows[i]->visible) continue;
        for (int j = 0; j < windows[i]->widget_count; j++) {
            if (windows[i]->widgets[j]) {
                windows[i]->widgets[j]->handle_hover(x, y);
            }
        }
    }

    mouse_left = left;
    mouse_right = right;
}

} // namespace gui
