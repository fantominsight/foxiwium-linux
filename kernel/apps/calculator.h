#pragma once
#include <stdint.h>
#include "../gui/window.h"
#include "../graphics/graphics.h"
#include "../graphics/font.h"

namespace calc {

constexpr int BTN_W = 56;
constexpr int BTN_H = 40;
constexpr int BTN_GAP = 4;
constexpr int COLS = 5;
constexpr int ROWS = 5;

struct CalcState {
    char display[64];
    int display_len;
    double accumulator;
    double operand;
    char pending_op;
    bool new_number;
    bool error;
};

static CalcState state;

inline void init() {
    state.display[0] = '0';
    state.display[1] = 0;
    state.display_len = 1;
    state.accumulator = 0;
    state.operand = 0;
    state.pending_op = 0;
    state.new_number = true;
    state.error = false;
}

inline void double_to_str(double val, char* buf) {
    if (val != val) { buf[0] = 'E'; buf[1] = 'r'; buf[2] = 'r'; buf[3] = 0; return; }

    bool neg = val < 0;
    if (neg) val = -val;

    if (val > 999999999.0) {
        buf[0] = 'O'; buf[1] = 'v'; buf[2] = 'f'; buf[3] = 0; return;
    }

    uint64_t int_part = (uint64_t)val;
    double frac = val - (double)int_part;

    char tmp[16];
    int i = 0;
    if (int_part == 0) {
        tmp[i++] = '0';
    } else {
        while (int_part > 0) {
            tmp[i++] = '0' + (int_part % 10);
            int_part /= 10;
        }
    }

    int j = 0;
    if (neg) buf[j++] = '-';
    while (i > 0) buf[j++] = tmp[--i];

    if (frac > 0.0001) {
        buf[j++] = '.';
        for (int d = 0; d < 6 && frac > 0.00001; d++) {
            frac *= 10;
            int digit = (int)frac;
            buf[j++] = '0' + digit;
            frac -= digit;
        }
    }
    buf[j] = 0;
}

inline void input_digit(char d) {
    if (state.error) return;
    if (state.new_number) {
        state.display[0] = d;
        state.display[1] = 0;
        state.display_len = 1;
        state.new_number = false;
    } else {
        if (state.display_len < 12) {
            state.display[state.display_len++] = d;
            state.display[state.display_len] = 0;
        }
    }
}

inline void input_decimal() {
    if (state.error) return;
    if (state.new_number) {
        state.display[0] = '0';
        state.display[1] = '.';
        state.display[2] = 0;
        state.display_len = 2;
        state.new_number = false;
        return;
    }
    for (int i = 0; i < state.display_len; i++) {
        if (state.display[i] == '.') return;
    }
    state.display[state.display_len++] = '.';
    state.display[state.display_len] = 0;
}

inline double parse_display() {
    double val = 0;
    bool neg = false;
    int i = 0;
    if (state.display[0] == '-') { neg = true; i = 1; }
    while (state.display[i] && state.display[i] != '.') {
        val = val * 10 + (state.display[i] - '0');
        i++;
    }
    if (state.display[i] == '.') {
        i++;
        double frac = 0;
        double place = 0.1;
        while (state.display[i]) {
            frac += (state.display[i] - '0') * place;
            place *= 0.1;
            i++;
        }
        val += frac;
    }
    return neg ? -val : val;
}

inline void calculate() {
    double b = parse_display();
    double result = state.accumulator;

    switch (state.pending_op) {
        case '+': result = state.accumulator + b; break;
        case '-': result = state.accumulator - b; break;
        case '*': result = state.accumulator * b; break;
        case '/':
            if (b == 0) { state.error = true; state.display[0] = 'E'; state.display[1] = 'r'; state.display[2] = 'r'; state.display[3] = 0; state.display_len = 3; return; }
            result = state.accumulator / b; break;
        case '%':
            if (b == 0) { state.error = true; state.display[0] = 'E'; state.display[1] = 'r'; state.display[2] = 'r'; state.display[3] = 0; state.display_len = 3; return; }
            result = (int)state.accumulator % (int)b; break;
    }

    state.accumulator = result;
    double_to_str(result, state.display);
    state.display_len = 0;
    while (state.display[state.display_len]) state.display_len++;
}

inline void input_op(char op) {
    if (state.error) return;
    if (state.pending_op && !state.new_number) {
        calculate();
    } else {
        state.accumulator = parse_display();
    }
    state.pending_op = op;
    state.new_number = true;
}

inline void input_equals() {
    if (state.error) return;
    if (state.pending_op) {
        calculate();
        state.pending_op = 0;
    }
    state.new_number = true;
}

inline void input_clear() {
    init();
}

inline void input_backspace() {
    if (state.error || state.new_number) return;
    if (state.display_len > 1) {
        state.display_len--;
        state.display[state.display_len] = 0;
    } else {
        state.display[0] = '0';
        state.display[1] = 0;
        state.display_len = 1;
        state.new_number = true;
    }
}

inline void input_percent() {
    if (state.error) return;
    double val = parse_display();
    val /= 100.0;
    double_to_str(val, state.display);
    state.display_len = 0;
    while (state.display[state.display_len]) state.display_len++;
}

inline void input_sign() {
    if (state.error) return;
    if (state.display[0] == '-') {
        for (int i = 1; state.display[i]; i++) state.display[i - 1] = state.display[i];
        state.display[state.display_len - 1] = 0;
        state.display_len--;
    } else if (state.display[0] != '0' || state.display_len > 1) {
        for (int i = state.display_len; i >= 0; i--) state.display[i + 1] = state.display[i];
        state.display[0] = '-';
        state.display_len++;
    }
}

struct ButtonDef {
    const char* label;
    Color bg;
    Color fg;
};

static int calc_hover = -1;

inline void draw(gui::Window* w) {
    int x = w->bounds.x + 1;
    int y = w->bounds.y + 30;
    int ww = w->bounds.w - 2;
    int wh = w->bounds.h - 30;

    graphics::fill_rect(x, y, ww, wh, Color(22, 22, 32));

    // Display area
    int disp_h = 60;
    graphics::fill_rect(x + 8, y + 8, ww - 16, disp_h, Color(16, 16, 26));

    // Pending operation
    if (state.pending_op) {
        char op_str[4] = {state.pending_op, 0};
        font::draw_string(x + 16, y + 14, op_str, Color(139, 92, 246));
    }

    // Display text
    int dw = font::text_width(state.display, 2);
    int dx = x + ww - 16 - dw;
    if (dx < x + 16) dx = x + 16;
    font::draw_string(dx, y + 30, state.display, Color(240, 240, 240), 2);

    // Buttons grid
    int btn_area_y = y + 8 + disp_h + 8;
    int grid_w = COLS * (BTN_W + BTN_GAP) - BTN_GAP;
    int grid_x = x + (ww - grid_w) / 2;

    // Proper button labels
    const char* btn_labels[25] = {
        "C",  "%",  "<<", "/", "+",
        "7",  "8",  "9",  "*", "-",
        "4",  "5",  "6",  "*", "+",
        "1",  "2",  "3",  "",  "=",
        "+/-","0",  ".",  "",  "=",
    };

    Color btn_colors[25] = {
        Color(200, 60, 60),  Color(70, 72, 90),   Color(70, 72, 90),   Color(139, 92, 246), Color(139, 92, 246),
        Color(50, 52, 70),   Color(50, 52, 70),   Color(50, 52, 70),   Color(139, 92, 246), Color(139, 92, 246),
        Color(50, 52, 70),   Color(50, 52, 70),   Color(50, 52, 70),   Color(139, 92, 246), Color(139, 92, 246),
        Color(50, 52, 70),   Color(50, 52, 70),   Color(50, 52, 70),   Color(50, 52, 70),   Color(99, 102, 241),
        Color(50, 52, 70),   Color(50, 52, 70),   Color(50, 52, 70),   Color(50, 52, 70),   Color(99, 102, 241),
    };

    for (int row = 0; row < ROWS; row++) {
        for (int col = 0; col < COLS; col++) {
            int idx = row * COLS + col;
            int bx = grid_x + col * (BTN_W + BTN_GAP);
            int by = btn_area_y + row * (BTN_H + BTN_GAP);
            int bw = BTN_W;
            int bh = BTN_H;

            // Special handling for merged buttons
            if (idx == 19 || idx == 24) continue; // skip = second col
            if (idx == 18) { bw = BTN_W * 2 + BTN_GAP; } // empty before =
            if (idx == 23) { bw = BTN_W * 2 + BTN_GAP; } // empty before =

            bool hover = (idx == calc_hover);
            Color bg = btn_colors[idx];
            if (hover) {
                bg = Color(bg.r + 30, bg.g + 30, bg.b + 30);
            }

            graphics::fill_rounded_rect(bx, by, bw, bh, 6, bg);

            const char* label = btn_labels[idx];
            if (label[0]) {
                int lw = font::text_width(label, 1);
                font::draw_string(bx + (bw - lw) / 2, by + 12, label, Color(240, 240, 240));
            }
        }
    }
}

inline bool handle_click(gui::Window* w, int mx, int my) {
    int x = w->bounds.x + 1;
    int y = w->bounds.y + 30;
    int ww = w->bounds.w - 2;
    (void)(w->bounds.h - 30);

    int disp_h = 60;
    int btn_area_y = y + 8 + disp_h + 8;
    int grid_w = COLS * (BTN_W + BTN_GAP) - BTN_GAP;
    int grid_x = x + (ww - grid_w) / 2;

    for (int row = 0; row < ROWS; row++) {
        for (int col = 0; col < COLS; col++) {
            int idx = row * COLS + col;
            if (idx == 19 || idx == 24 || idx == 18 || idx == 23) continue;

            int bx = grid_x + col * (BTN_W + BTN_GAP);
            int by = btn_area_y + row * (BTN_H + BTN_GAP);
            int bw = BTN_W;
            int bh = BTN_H;

            if (mx >= bx && mx < bx + bw && my >= by && my < by + bh) {
                // Handle button press
                if (idx == 0) input_clear();              // C
                else if (idx == 1) input_percent();        // %
                else if (idx == 2) input_backspace();      // <<
                else if (idx == 3) input_op('/');          // /
                else if (idx == 4) input_op('+');          // +
                else if (idx == 5) input_digit('7');
                else if (idx == 6) input_digit('8');
                else if (idx == 7) input_digit('9');
                else if (idx == 8) input_op('*');
                else if (idx == 9) input_op('-');
                else if (idx == 10) input_digit('4');
                else if (idx == 11) input_digit('5');
                else if (idx == 12) input_digit('6');
                else if (idx == 13) input_op('*');
                else if (idx == 14) input_op('+');
                else if (idx == 15) input_digit('1');
                else if (idx == 16) input_digit('2');
                else if (idx == 17) input_digit('3');
                else if (idx == 19) input_equals();
                else if (idx == 20) input_sign();
                else if (idx == 21) input_digit('0');
                else if (idx == 22) input_decimal();
                else if (idx == 24) input_equals();
                return true;
            }
        }
    }
    return false;
}

inline bool handle_hover(gui::Window* w, int mx, int my) {
    int x = w->bounds.x + 1;
    int y = w->bounds.y + 30;
    int ww = w->bounds.w - 2;

    int disp_h = 60;
    int btn_area_y = y + 8 + disp_h + 8;
    int grid_w = COLS * (BTN_W + BTN_GAP) - BTN_GAP;
    int grid_x = x + (ww - grid_w) / 2;

    calc_hover = -1;
    for (int row = 0; row < ROWS; row++) {
        for (int col = 0; col < COLS; col++) {
            int idx = row * COLS + col;
            if (idx == 19 || idx == 24 || idx == 18 || idx == 23) continue;

            int bx = grid_x + col * (BTN_W + BTN_GAP);
            int by = btn_area_y + row * (BTN_H + BTN_GAP);
            int bw = BTN_W;
            int bh = BTN_H;

            if (mx >= bx && mx < bx + bw && my >= by && my < by + bh) {
                calc_hover = idx;
                return true;
            }
        }
    }
    return false;
}

}
