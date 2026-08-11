#pragma once
#include <stdint.h>
#include "../gui/window.h"
#include "../graphics/graphics.h"
#include "../graphics/font.h"
#include "../drivers/ps2_keyboard.h"
#include "../drivers/pit.h"

namespace editor {

constexpr int MAX_TEXT = 8192;
constexpr int MAX_LINES = 256;
constexpr int LINE_HEIGHT = 16;
constexpr int MARGIN_LEFT = 50;

struct EditorState {
    char text[MAX_TEXT];
    int text_len;
    int cursor_pos;
    int scroll_y;
    int view_lines;
    int cursor_blink;
    char filename[64];
    bool modified;
    char status_msg[128];
    int status_tick;
};

static EditorState estate;

inline void init() {
    estate.text[0] = 0;
    estate.text_len = 0;
    estate.cursor_pos = 0;
    estate.scroll_y = 0;
    estate.view_lines = 20;
    estate.cursor_blink = 0;
    estate.filename[0] = 0;
    estate.modified = false;
    estate.status_msg[0] = 0;
    estate.status_tick = 0;
}

inline void set_filename(const char* name) {
    int i = 0;
    while (name[i] && i < 63) { estate.filename[i] = name[i]; i++; }
    estate.filename[i] = 0;
}

inline void set_status(const char* msg) {
    int i = 0;
    while (msg[i] && i < 127) { estate.status_msg[i] = msg[i]; i++; }
    estate.status_msg[i] = 0;
    estate.status_tick = 0;
}

inline int count_lines() {
    int lines = 1;
    for (int i = 0; i < estate.text_len; i++) {
        if (estate.text[i] == '\n') lines++;
    }
    return lines;
}

inline void insert_char(char c) {
    if (estate.text_len >= MAX_TEXT - 1) return;
    for (int i = estate.text_len; i > estate.cursor_pos; i--) {
        estate.text[i] = estate.text[i - 1];
    }
    estate.text[estate.cursor_pos] = c;
    estate.text_len++;
    estate.cursor_pos++;
    estate.text[estate.text_len] = 0;
    estate.modified = true;
}

inline void delete_char() {
    if (estate.cursor_pos <= 0) return;
    for (int i = estate.cursor_pos - 1; i < estate.text_len - 1; i++) {
        estate.text[i] = estate.text[i + 1];
    }
    estate.text_len--;
    estate.cursor_pos--;
    estate.text[estate.text_len] = 0;
    estate.modified = true;
}

inline void move_cursor_up() {
    if (estate.cursor_pos == 0) return;
    int line_start = estate.cursor_pos;
    while (line_start > 0 && estate.text[line_start - 1] != '\n') line_start--;
    if (line_start == 0) {
        estate.cursor_pos = 0;
        return;
    }
    int prev_line_end = line_start - 1;
    int prev_line_start = prev_line_end;
    while (prev_line_start > 0 && estate.text[prev_line_start - 1] != '\n') prev_line_start--;
    int col = estate.cursor_pos - line_start;
    int prev_len = prev_line_end - prev_line_start;
    estate.cursor_pos = prev_line_start + (col < prev_len ? col : prev_len);
}

inline void move_cursor_down() {
    int line_start = estate.cursor_pos;
    while (line_start < estate.text_len && estate.text[line_start] != '\n') line_start++;
    if (line_start >= estate.text_len) return;
    line_start++;
    int line_end = line_start;
    while (line_end < estate.text_len && estate.text[line_end] != '\n') line_end++;

    int col = estate.cursor_pos;
    int cur_line_start = estate.cursor_pos;
    while (cur_line_start > 0 && estate.text[cur_line_start - 1] != '\n') cur_line_start--;
    col -= cur_line_start;

    int next_len = line_end - line_start;
    estate.cursor_pos = line_start + (col < next_len ? col : next_len);
}

inline void move_cursor_home() {
    while (estate.cursor_pos > 0 && estate.text[estate.cursor_pos - 1] != '\n') {
        estate.cursor_pos--;
    }
}

inline void move_cursor_end() {
    while (estate.cursor_pos < estate.text_len && estate.text[estate.cursor_pos] != '\n') {
        estate.cursor_pos++;
    }
}

inline void page_up() {
    for (int i = 0; i < estate.view_lines; i++) move_cursor_up();
}

inline void page_down() {
    for (int i = 0; i < estate.view_lines; i++) move_cursor_down();
}

inline void handle_key(int scancode, char ascii) {
    if (ascii == '\b') {
        delete_char();
    } else if (scancode == 0x48) { // Up
        move_cursor_up();
    } else if (scancode == 0x50) { // Down
        move_cursor_down();
    } else if (scancode == 0x4B) { // Left
        if (estate.cursor_pos > 0) estate.cursor_pos--;
    } else if (scancode == 0x4D) { // Right
        if (estate.cursor_pos < estate.text_len) estate.cursor_pos++;
    } else if (scancode == 0x47) { // Home
        move_cursor_home();
    } else if (scancode == 0x4F) { // End
        move_cursor_end();
    } else if (ascii == '\n') {
        insert_char('\n');
    } else if (ascii >= 32 && ascii < 127) {
        insert_char(ascii);
    }
}

inline void load_content(const char* data, int size) {
    int i = 0;
    while (i < size && i < MAX_TEXT - 1) {
        estate.text[i] = data[i];
        i++;
    }
    estate.text_len = i;
    estate.text[estate.text_len] = 0;
    estate.cursor_pos = 0;
    estate.modified = false;
}

inline void draw(gui::Window* w) {
    int x = w->bounds.x + 1;
    int y = w->bounds.y + 30;
    int ww = w->bounds.w - 2;
    int wh = w->bounds.h - 30;

    // Background
    graphics::fill_rect(x, y, ww, wh, Color(18, 18, 28));

    // Line numbers gutter
    int gutter_w = MARGIN_LEFT;
    graphics::fill_rect(x, y, gutter_w, wh, Color(14, 14, 22));
    graphics::fill_rect(x + gutter_w, y, 1, wh, Color(40, 40, 55));

    // Text area
    int text_x = x + gutter_w + 8;
    int text_y = y + 4;
    int visible_lines = wh / LINE_HEIGHT;
    estate.view_lines = visible_lines;

    // Count lines and find cursor line
    int cursor_line = 0;
    int cursor_col = 0;

    // Build line info
    struct LineInfo { int start; int length; };
    LineInfo lines[MAX_LINES];
    int line_count = 0;

    int line_start = 0;
    for (int i = 0; i <= estate.text_len; i++) {
        if (i == estate.text_len || estate.text[i] == '\n') {
            if (line_count < MAX_LINES) {
                lines[line_count].start = line_start;
                lines[line_count].length = i - line_start;
                line_count++;
            }
            line_start = i + 1;
        }
    }

    // Find cursor position
    for (int i = 0; i < line_count; i++) {
        int line_end = lines[i].start + lines[i].length;
        if (estate.cursor_pos >= lines[i].start && estate.cursor_pos <= line_end) {
            cursor_line = i;
            cursor_col = estate.cursor_pos - lines[i].start;
            break;
        }
    }

    // Auto-scroll
    if (cursor_line < estate.scroll_y) estate.scroll_y = cursor_line;
    if (cursor_line >= estate.scroll_y + visible_lines) estate.scroll_y = cursor_line - visible_lines + 1;

    // Draw visible lines
    for (int i = 0; i < visible_lines && (i + estate.scroll_y) < line_count; i++) {
        int line_idx = i + estate.scroll_y;
        int ly = text_y + i * LINE_HEIGHT;
        int gutter_ly = y + 4 + i * LINE_HEIGHT;

        // Line number
        char num_buf[8];
        int num = line_idx + 1;
        int ni = 0;
        if (num == 0) { num_buf[ni++] = '0'; }
        else {
            char tmp[8]; int ti = 0;
            while (num > 0) { tmp[ti++] = '0' + num % 10; num /= 10; }
            while (ti > 0) num_buf[ni++] = tmp[--ti];
        }
        num_buf[ni] = 0;

        int nw = font::text_width(num_buf);
        font::draw_string(x + gutter_w - nw - 8, gutter_ly, num_buf,
                          (line_idx == cursor_line) ? Color(139, 92, 246) : Color(80, 80, 100));

        // Line content
        int line_len = lines[line_idx].length;
        int draw_len = 0;
        while (draw_len < line_len && draw_len < 100) {
            char c = estate.text[lines[line_idx].start + draw_len];
            font::draw_char(text_x + draw_len * 8, ly, c, Color(200, 200, 210));
            draw_len++;
        }
    }

    // Cursor
    estate.cursor_blink++;
    if ((estate.cursor_blink / 30) % 2 == 0) {
        int cy = text_y + (cursor_line - estate.scroll_y) * LINE_HEIGHT;
        int cx = text_x + cursor_col * 8;
        if (cy >= text_y && cy < text_y + visible_lines * LINE_HEIGHT) {
            graphics::fill_rect(cx, cy, 2, LINE_HEIGHT, Color(139, 92, 246));
        }
    }

    // Status bar
    int sb_y = y + wh - 22;
    graphics::fill_rect(x, sb_y, ww, 22, Color(24, 24, 34));
    graphics::fill_rect(x, sb_y, ww, 1, Color(40, 40, 55));

    // File name
    if (estate.filename[0]) {
        font::draw_string(x + 8, sb_y + 4, estate.filename,
                          estate.modified ? Color(255, 189, 46) : Color(140, 140, 160));
    } else {
        font::draw_string(x + 8, sb_y + 4, "[New File]", Color(140, 140, 160));
    }

    // Line:Col info
    char pos_buf[32] = "Ln ";
    int pi = 3;
    char tmp[8]; int ti = 0;
    int num = cursor_line + 1;
    if (num == 0) { tmp[ti++] = '0'; }
    else { while (num > 0) { tmp[ti++] = '0' + num % 10; num /= 10; } }
    while (ti > 0) pos_buf[pi++] = tmp[--ti];
    pos_buf[pi++] = ',';
    pos_buf[pi++] = ' ';
    pos_buf[pi++] = 'C';
    pos_buf[pi++] = 'o';
    pos_buf[pi++] = 'l';
    pos_buf[pi++] = ' ';
    num = cursor_col + 1;
    ti = 0;
    if (num == 0) { tmp[ti++] = '0'; }
    else { while (num > 0) { tmp[ti++] = '0' + num % 10; num /= 10; } }
    while (ti > 0) pos_buf[pi++] = tmp[--ti];
    pos_buf[pi] = 0;
    int pw = font::text_width(pos_buf);
    font::draw_string(x + ww - pw - 8, sb_y + 4, pos_buf, Color(140, 140, 160));

    // Total lines
    char total_buf[32] = "Total: ";
    int tl = 7;
    num = line_count;
    ti = 0;
    if (num == 0) { tmp[ti++] = '0'; }
    else { while (num > 0) { tmp[ti++] = '0' + num % 10; num /= 10; } }
    while (ti > 0) total_buf[tl++] = tmp[--ti];
    int tlw = font::text_width(total_buf);
    font::draw_string(x + ww - pw - tlw - 20, sb_y + 4, total_buf, Color(100, 100, 120));
}

}
