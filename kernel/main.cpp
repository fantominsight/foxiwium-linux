#include <stdint.h>
#include <stddef.h>

#define MB2_MAGIC 0x36d76289

#include "drivers/port.h"
#include "drivers/interrupts/pic.h"
#include "drivers/interrupts/idt.h"
#include "drivers/ps2_keyboard.h"
#include "drivers/ps2_mouse.h"
#include "drivers/pit.h"
#include "drivers/power.h"
#include "drivers/net.h"
#include "graphics/framebuffer.h"
#include "graphics/graphics.h"
#include "graphics/font.h"
#include "graphics/wallpaper.h"
#include "graphics/boot.h"
#include "gui/window.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "arch/gdt.h"
#include "arch/cpu.h"
#include "syscall/syscall.h"
#include "syscall/handlers.h"
#include "proc/process.h"
#include "fs/elf_loader.h"
#include "fs/initramfs.h"
#include "fs/vfs.h"
#include "userspace/shell_bin.h"
#include "apps/commands.h"
#include "apps/calculator.h"
#include "apps/editor.h"
#include "apps/settings.h"
#include "apps/about.h"
#include "apps/browser.h"
#include "apps/browser.h"

static void debug_putchar(char c) { port::outb(0xE9, c); }
static void debug_print(const char* s) { while (*s) debug_putchar(*s++); }
static void __attribute__((unused)) debug_print_hex(uint64_t v) {
    debug_print("0x");
    char buf[17]; buf[16]=0;
    for(int i=15;i>=0;i--){buf[i]="0123456789abcdef"[v&0xF];v>>=4;}
    debug_print(buf);
}

static void serial_init() {
    port::outb(0x3F8+1,0x00); port::outb(0x3F8+3,0x80);
    port::outb(0x3F8+0,0x03); port::outb(0x3F8+1,0x00);
    port::outb(0x3F8+3,0x03); port::outb(0x3F8+2,0xC7);
    port::outb(0x3F8+4,0x0B);
}
static void serial_print(const char* s) {
    while (*s) { while((port::inb(0x3F8+5)&0x20)==0); port::outb(0x3F8,*s++); }
}

extern "C" void syscall_entry();

ProcessControlBlock pcb;

extern "C" void idt_dispatch(idt::InterruptFrame* frame) {
    uint8_t vec = frame->int_no;
    if (vec == 32) { pit::handler(); }
    else if (vec == 33) { uint8_t sc = port::inb(0x60); keyboard::handle_scancode(sc); }
    else if (vec == 44) { mouse::handle_packet(); }
    else if (vec == 14) {
        uint64_t fault_va;
        asm volatile("mov %%cr2, %0" : "=r"(fault_va));
        uint64_t cr3v;
        asm volatile("mov %%cr3, %0" : "=r"(cr3v));
        uint64_t va = fault_va;
        debug_print("\n[PF] cr3=0x");
        char buf[16]; uint64_t v = cr3v;
        for (int i = 15; i >= 0; i--) { buf[i] = "0123456789abcdef"[v & 0xF]; v >>= 4; }
        for (int i = 0; i < 16; i++) debug_putchar(buf[i]);
        uint64_t* pml4 = (uint64_t*)cr3v;
        uint64_t pi = (va >> 39) & 0x1FF, di = (va >> 30) & 0x1FF,
                 ti = (va >> 21) & 0x1FF, fi = (va >> 12) & 0x1FF;
        debug_print("\n[PF] pml4[0x");
        v = pi;
        for (int i = 7; i >= 0; i--) { buf[i] = "0123456789abcdef"[v & 0xF]; v >>= 4; }
        for (int i = 0; i < 8; i++) debug_putchar(buf[i]);
        debug_print("]=");
        v = pml4[pi];
        for (int i = 15; i >= 0; i--) { buf[i] = "0123456789abcdef"[v & 0xF]; v >>= 4; }
        for (int i = 0; i < 16; i++) debug_putchar(buf[i]);
        if (pml4[pi] & 1) {
            uint64_t* pdpt = (uint64_t*)(pml4[pi] & ~0xFFFULL);
            debug_print("\n[PF] pdpt[0x");
            v = di;
            for (int i = 7; i >= 0; i--) { buf[i] = "0123456789abcdef"[v & 0xF]; v >>= 4; }
            for (int i = 0; i < 8; i++) debug_putchar(buf[i]);
            debug_print("]=");
            v = pdpt[di];
            for (int i = 15; i >= 0; i--) { buf[i] = "0123456789abcdef"[v & 0xF]; v >>= 4; }
            for (int i = 0; i < 16; i++) debug_putchar(buf[i]);
            if ((pdpt[di] & 1) && !(pdpt[di] & 0x80)) {
                uint64_t* pd = (uint64_t*)(pdpt[di] & ~0xFFFULL);
                debug_print("\n[PF] pd[0x");
                v = ti;
                for (int i = 7; i >= 0; i--) { buf[i] = "0123456789abcdef"[v & 0xF]; v >>= 4; }
                for (int i = 0; i < 8; i++) debug_putchar(buf[i]);
                debug_print("]=");
                v = pd[ti];
                for (int i = 15; i >= 0; i--) { buf[i] = "0123456789abcdef"[v & 0xF]; v >>= 4; }
                for (int i = 0; i < 16; i++) debug_putchar(buf[i]);
                if ((pd[ti] & 1) && !(pd[ti] & 0x80)) {
                    uint64_t* pt = (uint64_t*)(pd[ti] & ~0xFFFULL);
                    debug_print("\n[PF] pt[0x");
                    v = fi;
                    for (int i = 7; i >= 0; i--) { buf[i] = "0123456789abcdef"[v & 0xF]; v >>= 4; }
                    for (int i = 0; i < 8; i++) debug_putchar(buf[i]);
                    debug_print("]=");
                    v = pt[fi];
                    for (int i = 15; i >= 0; i--) { buf[i] = "0123456789abcdef"[v & 0xF]; v >>= 4; }
                    for (int i = 0; i < 16; i++) debug_putchar(buf[i]);
                }
            }
        }
        debug_print("\n[PAGE FAULT] err=0x");
        v = frame->err_code;
        for (int i = 15; i >= 0; i--) { buf[i] = "0123456789abcdef"[v & 0xF]; v >>= 4; }
        for (int i = 0; i < 16; i++) debug_putchar(buf[i]);
        debug_print(" at 0x");
        v = fault_va;
        for (int i = 15; i >= 0; i--) { buf[i] = "0123456789abcdef"[v & 0xF]; v >>= 4; }
        for (int i = 0; i < 16; i++) debug_putchar(buf[i]);
        debug_print(" rip=0x");
        v = frame->rip;
        for (int i = 15; i >= 0; i--) { buf[i] = "0123456789abcdef"[v & 0xF]; v >>= 4; }
        for (int i = 0; i < 16; i++) debug_putchar(buf[i]);
        debug_print("\n[FATAL] Halting.\n");
        while(true) asm volatile("cli; hlt");
    }
    else if (vec == 13) {
        debug_print("[GPF] err=0x");
        char buf[16]; uint64_t v = frame->err_code;
        for (int i = 15; i >= 0; i--) { buf[i] = "0123456789abcdef"[v & 0xF]; v >>= 4; }
        for (int i = 0; i < 16; i++) debug_putchar(buf[i]);
        debug_print(" rip=0x");
        v = frame->rip;
        for (int i = 15; i >= 0; i--) { buf[i] = "0123456789abcdef"[v & 0xF]; v >>= 4; }
        for (int i = 0; i < 16; i++) debug_putchar(buf[i]);
        debug_print("\n[FATAL] Halting.\n");
        while(true) asm volatile("cli; hlt");
    }
    else if (vec < 32) {
        debug_print("[EXC 0x");
        char buf[16]; uint64_t v = vec;
        for (int i = 1; i >= 0; i--) { buf[i] = "0123456789abcdef"[v & 0xF]; v >>= 4; }
        for (int i = 0; i < 2; i++) debug_putchar(buf[i]);
        debug_print("] rip=0x");
        v = frame->rip;
        for (int i = 15; i >= 0; i--) { buf[i] = "0123456789abcdef"[v & 0xF]; v >>= 4; }
        for (int i = 0; i < 16; i++) debug_putchar(buf[i]);
        debug_print("\n[FATAL] Halting.\n");
        while(true) asm volatile("cli; hlt");
    }
    pic::eoi(vec);
}

extern "C" uint64_t syscall_entry_handler(uint64_t rdi, uint64_t rsi, uint64_t rdx,
                                           uint64_t r10, uint64_t r8, uint64_t r9,
                                           uint64_t syscall_num) {
    return syscall::dispatch(rdi, rsi, rdx, r10, r8, r9, syscall_num);
}

// ==================== THEME (KDE Breeze Dark) ====================
namespace theme {
    constexpr int PANEL_H      = 48;
    constexpr int WIN_TITLE_H  = 32;

    constexpr Color BG_PANEL     {49, 54, 59};      // #31363b
    constexpr Color BG_PANEL_HVR {58, 64, 70};      // hover
    constexpr Color ACCENT       {61, 174, 233};     // #3daee9 Breeze Blue
    constexpr Color TEXT_W       {240, 240, 240};    // white text
    constexpr Color TEXT_DIM     {160, 160, 170};    // dim text
    constexpr Color WIN_BG       {35, 38, 41};       // #232629
    constexpr Color WIN_TITLE    {35, 38, 41};       // same as bg in Breeze
    constexpr Color WIN_TITLE_IN {43, 47, 50};       // unfocused
    constexpr Color WIN_BORDER   {61, 174, 233};     // blue border when focused
    constexpr Color WIN_BORDER_N {50, 54, 59};       // unfocused border
    constexpr Color BTN_CLOSE    {237, 21, 21};      // Breeze close red
    constexpr Color BTN_MINIMIZE {246, 116, 0};      // Breeze minimize orange
    constexpr Color BTN_MAXIMIZE {39, 174, 96};      // Breeze maximize green
    constexpr Color SIDEBAR      {30, 33, 36};       // sidebar bg
    constexpr Color HOVER        {45, 50, 56};       // hover bg
    constexpr Color SELECT       {61, 174, 233};     // selection = accent
    constexpr Color FOLDER       {61, 174, 233};     // folders = accent
    constexpr Color FILE_CLR     {160, 160, 170};
    constexpr Color DESKTOP_BG   {35, 38, 41};       // wallpaper base

    // ---- Liquid Glass ----
    constexpr int    RADIUS        = 14;              // window corner radius
    constexpr Color  GLASS_WIN     {37, 40, 46, 205}; // translucent window body
    constexpr Color  GLASS_TITLE   {48, 52, 60, 235}; // translucent title bar
    constexpr Color  GLASS_PANEL   {38, 42, 48, 220}; // translucent bottom panel
    constexpr Color  GLASS_MENU    {34, 37, 43, 238}; // launcher / popup menu
    constexpr Color  GLASS_HOVER   {255, 255, 255, 30};
    constexpr Color  GLASS_EDGE    {255, 255, 255, 26};
    constexpr Color  GLASS_SHINE   {255, 255, 255, 42};
    constexpr Color  GLASS_TASK_ON {61, 174, 233, 100}; // active task pill
    constexpr Color  GLASS_TASK    {255, 255, 255, 16}; // idle task pill
    constexpr Color  GLASS_SCROLL  {255, 255, 255, 20};
}

// ==================== UTILS ====================
static void itoa_dec(int val, char* buf) {
    if (val == 0) { buf[0]='0'; buf[1]=0; return; }
    char tmp[16]; int i=0; bool neg=val<0;
    if(neg) val=-val;
    while(val>0) { tmp[i++]='0'+val%10; val/=10; }
    if(neg) tmp[i++]='-';
    int j=0; while(i>0) buf[j++]=tmp[--i];
    buf[j]=0;
}

static int str_len(const char* s) { int l=0; while(*s++)l++; return l; }

// ==================== WALLPAPER (embedded image) ====================
static uint32_t wallpaper_cache[1280 * 720];
static bool wallpaper_cached = false;

static void cache_wallpaper() {
    uint32_t w = framebuffer::get_width();
    uint32_t h = framebuffer::get_height();
    uint32_t pitch = framebuffer::get_pitch() / 4;

    for (uint32_t y = 0; y < h && y < WALLPAPER_H; y++) {
        for (uint32_t x = 0; x < w && x < WALLPAPER_W; x++) {
            wallpaper_cache[y * pitch + x] = wallpaper_pixels[y * WALLPAPER_W + x];
        }
    }

    wallpaper_cached = true;
}

static void draw_wallpaper() {
    uint32_t* dst = framebuffer::get_buffer();
    const uint32_t* src = wallpaper_cache;
    uint32_t count = (framebuffer::get_pitch() / 4) * framebuffer::get_height();
    __asm__ volatile(
        "rep movsl"
        : "+D"(dst), "+S"(src), "+c"(count)
        :
        : "memory"
    );
}

// ==================== BOOT SPLASH (Windows-like) ====================

// Small blue blinking square below the "Foxiwium" text while booting
static void draw_boot_blinker(bool on) {
    uint32_t fw = framebuffer::get_width();
    uint32_t fh = framebuffer::get_height();

    int img = BOOT_IMAGE_W;
    int text_top = ((int)fh - img) / 2 + img + 24;
    int size = 16;
    int x0 = ((int)fw - size) / 2;
    int y0 = text_top + font::text_height(2) + 16 - size / 2;
    if (y0 < 0) y0 = 0;

    uint32_t color = on ? 0xFF3399FF : 0xFF000000;
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            framebuffer::put_pixel(x0 + x, y0 + y, color);
        }
    }
    framebuffer::flip();
}

static void boot_pause_ms(uint32_t ms) {
    uint32_t lo, hi;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    uint64_t start = ((uint64_t)hi << 32) | lo;
    uint64_t cycles = (uint64_t)ms * 1000000;
    uint64_t interval = (uint64_t)500 * 1000000;
    bool on = false;
    while (true) {
        asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
        uint64_t now = ((uint64_t)hi << 32) | lo;
        uint64_t elapsed = now - start;
        if (elapsed >= cycles) break;
        bool want = ((elapsed / interval) % 2) == 0;
        if (want != on) {
            on = want;
            draw_boot_blinker(on);
        }
        asm volatile("pause");
    }
}

static void draw_boot_screen() {
    uint32_t fw = framebuffer::get_width();
    uint32_t fh = framebuffer::get_height();

    framebuffer::clear(0xFF000000);

    int img = BOOT_IMAGE_W;
    int ix = ((int)fw - img) / 2;
    int iy = ((int)fh - img) / 2;

    uint32_t* buf = framebuffer::get_buffer();
    uint32_t fpitch = framebuffer::get_pitch() / 4;
    for (int y = 0; y < img; y++) {
        int ty = iy + y;
        if (ty < 0 || ty >= (int)fh) continue;
        for (int x = 0; x < img; x++) {
            int tx = ix + x;
            if (tx < 0 || tx >= (int)fw) continue;
            buf[ty * fpitch + tx] = boot_image_pixels[y * img + x];
        }
    }

    font::draw_string_centered(fw / 2, iy + img + 24, "Foxiwium", theme::TEXT_W, 2);

    framebuffer::flip();
}

// ==================== PANEL (KDE Plasma bottom panel) ====================
static int g_hour = 12, g_min = 0;

// Forward-declare desktop as file-scope so panel/launcher can reference it
static gui::Desktop desktop;

// App IDs (kept for window tracking)
enum AppID {
    APP_FILES = 0,
    APP_TERMINAL,
    APP_MONITOR,
    APP_CALCULATOR,
    APP_EDITOR,
    APP_SETTINGS,
    APP_ABOUT,
    APP_BROWSER,
    APP_COUNT
};

struct AppInfo {
    const char* icon;
    const char* label;
    Color accent;
    bool running;
    int window_idx;
};

static AppInfo app_list[APP_COUNT] = {
    {"F", "Files",       theme::FOLDER,    false,  -1},
    {"T", "Terminal",    theme::ACCENT,    false,  -1},
    {"M", "Monitor",     theme::BTN_MAXIMIZE, false,  -1},
    {"=", "Calculator",  theme::ACCENT,    false,  -1},
    {"E", "Editor",      theme::BTN_MINIMIZE, false,  -1},
    {"S", "Settings",    theme::TEXT_DIM,  false,  -1},
    {"A", "About",       theme::ACCENT,    false,  -1},
    {"W", "Browser",     theme::ACCENT,    false,  -1},
};

// Launcher state
static bool launcher_open = false;
static int launcher_hover = -1;
static int launcher_cat_hover = 0;
static char launcher_search[32] = {};
static int launcher_search_len = 0;

// Context menu state
static bool ctx_menu_open = false;
static int ctx_menu_hover = -1;

// Panel hover
static int panel_task_hover = -1;

// Power menu state
static bool power_menu_open = false;
static int power_menu_hover = -1;

// System sleep
static bool system_sleeping = false;

// Draw a power symbol (vertical line + circle outline)
static void draw_power_icon(int cx, int cy, Color c) {
    graphics::draw_circle(cx, cy + 1, 6, c);
    graphics::fill_rect(cx - 1, cy - 8, 2, 9, c);
}

static void draw_panel() {
    uint32_t fw = framebuffer::get_width();
    uint32_t fh = framebuffer::get_height();
    int py = fh - theme::PANEL_H;

    // Glass panel (rounded top corners, translucent)
    graphics::fill_rounded_rect_top_alpha(0, py, fw, theme::PANEL_H, theme::RADIUS, theme::GLASS_PANEL);
    // Top shine line
    graphics::fill_rect_alpha(0, py, fw, 1, theme::GLASS_SHINE);

    // --- Left: Launcher button (diamond logo) ---
    int lx = 0;
    int lw = 56;
    int lc_x = lx + lw / 2;
    int lc_y = py + theme::PANEL_H / 2;
    bool launcher_hovered = (mouse::cursor_x >= lx && mouse::cursor_x < lx + lw &&
                             mouse::cursor_y >= py && mouse::cursor_y < py + (int)fh);
    if (launcher_hovered || launcher_open) {
        graphics::fill_rounded_rect_alpha(lx + 4, py + 4, lw - 8, theme::PANEL_H - 8, 12,
            launcher_open ? Color(theme::ACCENT.r, theme::ACCENT.g, theme::ACCENT.b, 210)
                          : theme::GLASS_HOVER);
    }
    // Draw a simple diamond/hexagon logo
    Color logo_c = launcher_open ? theme::TEXT_W : theme::ACCENT;
    int hs = 10;
    int pts[][2] = {
        {lc_x, lc_y - hs},
        {lc_x + hs, lc_y},
        {lc_x, lc_y + hs},
        {lc_x - hs, lc_y}
    };
    for (int i = 0; i < 4; i++) {
        int nx = (i + 1) % 4;
        graphics::draw_line(pts[i][0], pts[i][1], pts[nx][0], pts[nx][1], logo_c);
    }
    // Fill diamond lightly
    for (int dy = -hs; dy <= hs; dy++) {
        for (int dx = -hs; dx <= hs; dx++) {
            if (dx * dx + dy * dy <= hs * hs && (dx + dy) % 2 == 0) {
                graphics::put_pixel(lc_x + dx, lc_y + dy, logo_c);
            }
        }
    }

    // --- Center: Task buttons for open windows (glass pills) ---
    int task_x = lx + lw + 8;
    int task_idx = 0;
    for (int i = 0; i < APP_COUNT; i++) {
        if (!app_list[i].running) continue;
        if (app_list[i].window_idx < 0 || app_list[i].window_idx >= desktop.window_count) continue;

        int tw = font::text_width(app_list[i].label) + 24;
        bool active_task = (desktop.active_window == app_list[i].window_idx);
        bool hov = (task_idx == panel_task_hover);

        Color task_bg = active_task ? theme::GLASS_TASK_ON
                      : hov ? theme::GLASS_HOVER
                      : theme::GLASS_TASK;
        graphics::fill_rounded_rect_alpha(task_x, py + 4, tw, theme::PANEL_H - 8, 10, task_bg);

        // Accent underline for active
        if (active_task) {
            graphics::fill_rect(task_x + 4, py + theme::PANEL_H - 6, tw - 8, 2, theme::ACCENT);
        }

        // Icon letter
        font::draw_string(task_x + 6, py + 14, app_list[i].icon, app_list[i].accent);
        // Label
        font::draw_string(task_x + 20, py + 14, app_list[i].label, theme::TEXT_W);

        task_x += tw + 4;
        task_idx++;
    }

    // --- Right: System tray + Power + Clock ---
    // Separator
    int tray_x = fw - 180;
    graphics::fill_rect_alpha(tray_x - 1, py + 8, 1, theme::PANEL_H - 16, theme::GLASS_EDGE);

    // Status indicators
    font::draw_string(tray_x + 8, py + 14, "RAM", theme::TEXT_DIM);
    uint64_t used_mem = pmm::get_used_mb();
    char memstr[16];
    itoa_dec((int)used_mem, memstr);
    font::draw_string(tray_x + 36, py + 14, memstr, theme::TEXT_DIM);

    // Power button
    int pbtn_s = 32;
    int pbtn_x = fw - 98;
    int pbtn_y = py + (theme::PANEL_H - pbtn_s) / 2;
    bool pbtn_hover = (mouse::cursor_x >= pbtn_x && mouse::cursor_x < pbtn_x + pbtn_s &&
                       mouse::cursor_y >= py && mouse::cursor_y < py + (int)fh);
    if (power_menu_open) {
        graphics::fill_rounded_rect_alpha(pbtn_x, pbtn_y, pbtn_s, pbtn_s, 10, Color(220, 60, 47, 210));
    } else if (pbtn_hover) {
        graphics::fill_rounded_rect_alpha(pbtn_x, pbtn_y, pbtn_s, pbtn_s, 10, theme::GLASS_HOVER);
    }
    draw_power_icon(pbtn_x + pbtn_s / 2, pbtn_y + pbtn_s / 2, theme::TEXT_W);

    // Clock
    char clock[8] = {};
    clock[0] = '0' + g_hour / 10;
    clock[1] = '0' + g_hour % 10;
    clock[2] = ':';
    clock[3] = '0' + g_min / 10;
    clock[4] = '0' + g_min % 10;
    clock[5] = 0;
    font::draw_string(fw - 56, py + 14, clock, theme::TEXT_W);
}

// ==================== APP LAUNCHER (KDE Kickoff style) ====================
static void draw_launcher() {
    if (!launcher_open) return;

    uint32_t fh = framebuffer::get_height();
    int menu_w = 420;
    int menu_h = 360;
    int menu_x = 0;
    int menu_y = fh - theme::PANEL_H - menu_h;
    int sbw = 100;

    // Glass background
    graphics::fill_rounded_rect_alpha(menu_x, menu_y, menu_w, menu_h, 16, theme::GLASS_MENU);
    graphics::draw_rounded_rect_alpha(menu_x, menu_y, menu_w, menu_h, 16, theme::GLASS_EDGE);

    // Left sidebar (darker glass)
    graphics::fill_rect_alpha(menu_x, menu_y, sbw, menu_h, Color(0, 0, 0, 70));
    graphics::fill_rect_alpha(menu_x + sbw, menu_y, 1, menu_h, theme::GLASS_EDGE);

    const char* categories[] = {"Favorites", "Applications", "Places", "System"};
    for (int i = 0; i < 4; i++) {
        int sy = menu_y + 12 + i * 28;
        if (i == launcher_cat_hover) {
            graphics::fill_rounded_rect_alpha(menu_x + 4, sy - 2, sbw - 8, 24, 8,
                Color(theme::ACCENT.r, theme::ACCENT.g, theme::ACCENT.b, 190));
        }
        font::draw_string(menu_x + 12, sy + 3, categories[i],
                         i == launcher_cat_hover ? theme::TEXT_W : theme::TEXT_DIM);
    }

    // Main area: app list
    int ax = menu_x + sbw + 8;
    int ay = menu_y + 12;
    int item_h = 32;
    int app_count = APP_COUNT;

    for (int i = 0; i < app_count; i++) {
        int iy = ay + i * item_h;
        if (iy + item_h > menu_y + menu_h - 44) break;

        bool hov = (i == launcher_hover);
        if (hov) {
            graphics::fill_rounded_rect_alpha(ax - 4, iy, menu_w - sbw - 8, item_h - 2, 8,
                theme::GLASS_HOVER);
        }

        // Icon circle
        graphics::fill_circle(ax + 12, iy + 12, 10, app_list[i].accent);
        // Icon letter
        int iw = font::text_width(app_list[i].icon);
        font::draw_string(ax + 12 - iw / 2, iy + 4, app_list[i].icon, theme::TEXT_W);

        // Label
        font::draw_string(ax + 30, iy + 8, app_list[i].label, theme::TEXT_W);

        // Running indicator
        if (app_list[i].running) {
            graphics::fill_circle(ax + menu_w - sbw - 20, iy + 12, 3, theme::ACCENT);
        }
    }

    // Search bar at bottom
    int search_y = menu_y + menu_h - 40;
    graphics::fill_rounded_rect_alpha(ax - 4, search_y, menu_w - sbw - 8, 32, 8, Color(0, 0, 0, 80));
    graphics::draw_rounded_rect_alpha(ax - 4, search_y, menu_w - sbw - 8, 32, 8, theme::GLASS_EDGE);
    const char* search_text = launcher_search_len > 0 ? launcher_search : "Search...";
    Color search_color = launcher_search_len > 0 ? theme::TEXT_W : theme::TEXT_DIM;
    font::draw_string(ax + 4, search_y + 8, search_text, search_color);
    // Blinking cursor
    static int search_blink = 0;
    search_blink++;
    if ((search_blink / 30) % 2 == 0) {
        graphics::fill_rect(ax + 4 + launcher_search_len * 8, search_y + 8, 1, 14, theme::ACCENT);
    }
}

static void draw_ctx_menu() {
    if (!ctx_menu_open) return;

    int mx = mouse::cursor_x;
    int my = mouse::cursor_y;
    int mw = 180;
    int mh = 88;
    int item_h = 28;

    // Clamp to screen
    uint32_t fw = framebuffer::get_width();
    uint32_t fh = framebuffer::get_height();
    int cmx = mx;
    int cmy = my;
    if (cmx + mw > (int)fw) cmx = fw - mw;
    if (cmy + mh > (int)fh - theme::PANEL_H) cmy = fh - theme::PANEL_H - mh;

    // Glass background
    graphics::fill_rounded_rect_alpha(cmx, cmy, mw, mh, 12, theme::GLASS_MENU);
    graphics::draw_rounded_rect_alpha(cmx, cmy, mw, mh, 12, theme::GLASS_EDGE);

    const char* items[] = {"Configure Desktop", "Open Terminal", "Leave"};
    for (int i = 0; i < 3; i++) {
        int iy = cmy + 4 + i * item_h;
        bool hov = (i == ctx_menu_hover);
        if (hov) {
            graphics::fill_rounded_rect_alpha(cmx + 4, iy, mw - 8, item_h - 4, 8, theme::GLASS_HOVER);
        }
        font::draw_string(cmx + 12, iy + 6, items[i],
                         hov ? theme::ACCENT : theme::TEXT_W);
    }
}

// ==================== POWER MENU ====================
static void draw_power_menu() {
    if (!power_menu_open) return;

    uint32_t fw = framebuffer::get_width();
    uint32_t fh = framebuffer::get_height();
    int mw = 190;
    int item_h = 42;
    int mh = 8 + 3 * item_h + 8;
    int pmx = fw - mw - 8;
    int pmy = fh - theme::PANEL_H - mh - 8;

    graphics::fill_rounded_rect_alpha(pmx, pmy, mw, mh, 14, theme::GLASS_MENU);
    graphics::draw_rounded_rect_alpha(pmx, pmy, mw, mh, 14, theme::GLASS_EDGE);

    const char* items[] = {"Sleep", "Restart", "Power Off"};
    Color dots[] = {theme::ACCENT, theme::BTN_MINIMIZE, theme::BTN_CLOSE};
    for (int i = 0; i < 3; i++) {
        int iy = pmy + 8 + i * item_h;
        if (i == power_menu_hover) {
            graphics::fill_rounded_rect_alpha(pmx + 6, iy, mw - 12, item_h - 4, 10, theme::GLASS_HOVER);
        }
        graphics::fill_circle(pmx + 24, iy + item_h / 2 - 5, 4, dots[i]);
        font::draw_string(pmx + 40, iy + 13, items[i],
                          i == power_menu_hover ? theme::TEXT_W : theme::TEXT_DIM);
    }
}

// Handle launcher keyboard input
static void handle_launcher_key(char c) {
    if (!launcher_open) return;
    if (c == '\b' && launcher_search_len > 0) {
        launcher_search_len--;
        launcher_search[launcher_search_len] = 0;
    } else if (c && launcher_search_len < 31) {
        launcher_search[launcher_search_len++] = c;
        launcher_search[launcher_search_len] = 0;
    }
}
// ==================== WINDOW DRAWING (Liquid Glass) ====================
static void draw_window(gui::Window* w) {
    if (!w || !w->visible || !w->active) return;

    int x = w->bounds.x;
    int y = w->bounds.y;
    int ww = w->bounds.w;
    int wh = w->bounds.h;
    int th = theme::WIN_TITLE_H;
    int R = theme::RADIUS;

    // Soft glass shadow (multiple alpha layers over whatever is behind)
    for (int s = 5; s >= 1; s--) {
        graphics::fill_rounded_rect_alpha(x + s, y + s, ww, wh, R,
            Color(0, 0, 0, (uint8_t)(12 * (6 - s))));
    }

    // Translucent rounded body
    graphics::fill_rounded_rect_alpha(x, y, ww, wh, R, theme::GLASS_WIN);

    // Translucent title bar (rounded top corners)
    graphics::fill_rounded_rect_top_alpha(x, y, ww, th, R, theme::GLASS_TITLE);

    // Glass rim
    Color rim = w->focused
        ? Color(theme::ACCENT.r, theme::ACCENT.g, theme::ACCENT.b, 150)
        : theme::GLASS_EDGE;
    graphics::draw_rounded_rect_alpha(x, y, ww, wh, R, rim);

    // Shine line under the title bar
    graphics::fill_rect_alpha(x + R, y + th, ww - 2 * R, 1, theme::GLASS_SHINE);

    // Window buttons on RIGHT side (KDE style: close, maximize, minimize)
    int btn_y = y + 6;
    int btn_size = 20;
    int btn_gap = 2;
    // Close button (rightmost)
    int close_x = x + ww - btn_size - 6;
    graphics::fill_rounded_rect_alpha(close_x, btn_y, btn_size, btn_size, 6, theme::BTN_CLOSE);
    // Draw X
    font::draw_string(close_x + 5, btn_y + 3, "x", theme::TEXT_W);
    // Maximize button
    int max_x = close_x - btn_size - btn_gap;
    graphics::fill_rounded_rect_alpha(max_x, btn_y, btn_size, btn_size, 6, theme::BTN_MAXIMIZE);
    // Draw square
    graphics::draw_rect(max_x + 5, btn_y + 5, 10, 10, theme::TEXT_W, 1);
    // Minimize button
    int min_x = max_x - btn_size - btn_gap;
    graphics::fill_rounded_rect_alpha(min_x, btn_y, btn_size, btn_size, 6, theme::BTN_MINIMIZE);
    // Draw dash
    graphics::fill_rect(min_x + 5, btn_y + 9, 10, 2, theme::TEXT_W);

    // Title text (left-aligned, KDE style)
    font::draw_string(x + 12, y + 9, w->title, theme::TEXT_W);

    if (w->on_draw) w->on_draw(w);
}

// ==================== FILE MANAGER (VFS-based) ====================
struct FMState {
    int current_dir;
    int scroll;
    int selected;
    int hover;
    char path[vfs::MAX_PATH];
    bool sb_dragging = false;
    int sb_grab = 0;
};

static FMState fm_state;

inline void fm_init() {
    fm_state.current_dir = vfs::get_root();
    fm_state.scroll = 0;
    fm_state.selected = -1;
    fm_state.hover = -1;
    fm_state.path[0] = '/';
    fm_state.path[1] = 0;
}

inline void fm_navigate(int dir_idx) {
    vfs::VfsNode* node = vfs::get_node(dir_idx);
    if (!node || node->type != vfs::NODE_DIR) return;
    fm_state.current_dir = dir_idx;
    fm_state.scroll = 0;
    fm_state.selected = -1;
    fm_state.hover = -1;

    // Build path string
    char rev[vfs::MAX_PATH];
    int ri = 0;
    int cur = dir_idx;
    while (cur >= 0 && cur != vfs::get_root()) {
        const char* name = vfs::get_node(cur)->name;
        int nl = str_len(name);
        for (int i = nl - 1; i >= 0; i--) rev[ri++] = name[i];
        rev[ri++] = '/';
        cur = vfs::get_node(cur)->parent;
    }
    if (ri == 0) {
        fm_state.path[0] = '/';
        fm_state.path[1] = 0;
    } else {
        for (int i = 0; i < ri; i++) fm_state.path[i] = rev[ri - 1 - i];
        fm_state.path[ri] = 0;
    }
}

inline void fm_go_up() {
    vfs::VfsNode* cur = vfs::get_node(fm_state.current_dir);
    if (cur && cur->parent >= 0) {
        fm_navigate(cur->parent);
    }
}

static int fm_sidebar_dir(int i) {
    switch (i) {
        case 0: return vfs::get_root();
        case 1: return vfs::resolve_path("/home/user");
        case 2: return vfs::resolve_path("/home/user/Documents");
        case 3: return vfs::resolve_path("/home/user/Downloads");
        case 4: return vfs::resolve_path("/home/user/Music");
        case 5: return vfs::resolve_path("/home/user/Pictures");
        case 6: return vfs::resolve_path("/home/user/Videos");
        default: return -1;
    }
}

// Content geometry shared by draw/hover/click (values match fm_draw).
static void fm_grid_geom(gui::Window* w, int* cols, int* fx, int* fy, int* fw_area, int* count) {
    int x = w->bounds.x;
    int y = w->bounds.y + theme::WIN_TITLE_H + 32;
    int ww = w->bounds.w;
    int sbw = 160;
    *fw_area = ww - sbw - 24;
    *cols = *fw_area / 90;
    if (*cols < 1) *cols = 1;
    *fx = x + sbw + 12;
    *fy = y + 12;
    int children[32];
    *count = vfs::get_children(fm_state.current_dir, children, 32);
}

static void fm_handle_click(gui::Window* w, int mx, int my) {
    int x = w->bounds.x;
    int y = w->bounds.y + theme::WIN_TITLE_H;
    int ww = w->bounds.w;

    // Toolbar: Up button (top-right of the path bar)
    if (my >= y && my < y + 32) {
        int ub_x = x + ww - 44;
        if (mx >= ub_x && mx < ub_x + 36) {
            fm_go_up();
            return;
        }
        return;
    }

    // Sidebar
    int sbw = 160;
    if (mx >= x && mx < x + sbw && my >= y + 32) {
        int i = (my - (y + 32) - 6) / 28;
        if (i >= 0 && i < 8) {
            int d = fm_sidebar_dir(i);
            if (d >= 0) fm_navigate(d);
        }
        return;
    }

    // File grid
    int cols, fx, fy, fw_area, count;
    fm_grid_geom(w, &cols, &fx, &fy, &fw_area, &count);
    int children[32];
    vfs::get_children(fm_state.current_dir, children, 32);
    for (int i = 0; i < count; i++) {
        int col = i % cols;
        int row = i / cols;
        int ix = fx + col * 90 - 4;
        int iy = fy + row * 80 - fm_state.scroll - 4;
        if (mx >= ix && mx < ix + 84 && my >= iy && my < iy + 76) {
            fm_state.selected = i;
            vfs::VfsNode* child = vfs::get_node(children[i]);
            if (child && child->type == vfs::NODE_DIR) {
                debug_print("[FM] navigate dir idx=");
                char db[16]; int dv = children[i]; int di2 = 0; if (dv == 0) db[di2++]='0';
                while (dv > 0) { db[di2++] = '0' + dv % 10; dv /= 10; } db[di2] = 0;
                for (int k = 0; k < di2/2; k++) { char t=db[k]; db[k]=db[di2-1-k]; db[di2-1-k]=t; }
                debug_print(db); debug_print("\n");
                fm_navigate(children[i]);
            }
            return;
        }
    }
    fm_state.selected = -1;
}

static void fm_draw(gui::Window* w) {
    int x = w->bounds.x;
    int y = w->bounds.y + theme::WIN_TITLE_H;
    int ww = w->bounds.w;
    int wh = w->bounds.h - theme::WIN_TITLE_H;

    // Path bar
    graphics::fill_rect(x, y, ww, 32, theme::SIDEBAR);
    font::draw_string(x + 12, y + 8, fm_state.path, theme::TEXT_DIM);
    graphics::fill_rounded_rect(x + ww - 44, y + 4, 36, 24, 6, theme::SELECT);
    font::draw_string(x + ww - 34, y + 8, "\x18", theme::TEXT_W);
    graphics::fill_rect(x, y + 31, ww, 1, theme::WIN_BORDER);
    y += 32;
    wh -= 32;

    // Sidebar
    int sbw = 160;
    graphics::fill_rect(x, y, sbw, wh, theme::SIDEBAR);
    graphics::fill_rect(x + sbw, y, 1, wh, theme::WIN_BORDER);

    const char* sidebar_items[] = {"Root", "Home", "Documents", "Downloads", "Music", "Pictures", "Videos", "Trash"};
    Color sidebar_icons[] = {theme::FOLDER, theme::FOLDER, theme::FOLDER, Color(52,199,89),
                             Color(198,120,221), Color(52,199,89), theme::BTN_MINIMIZE, theme::TEXT_DIM};

    for (int i = 0; i < 8; i++) {
        int sy = y + 8 + i * 28;
        bool sel = (fm_sidebar_dir(i) == fm_state.current_dir);
        if (sel) {
            graphics::fill_rounded_rect(x + 4, sy - 2, sbw - 8, 24, 6, theme::SELECT);
        }
        graphics::fill_rect(x + 12, sy + 4, 12, 12, sidebar_icons[i]);
        font::draw_string(x + 30, sy + 3, sidebar_items[i], sel ? theme::TEXT_W : theme::TEXT_DIM);
    }

    // File list
    int fx = x + sbw + 12;
    int fy = y + 12;
    int fw_area = ww - sbw - 24;
    int cols = fw_area / 90;
    if (cols < 1) cols = 1;

    // Get directory entries
    int children[32];
    int count = vfs::get_children(fm_state.current_dir, children, 32);
    int visible_rows = (wh - 24) / 80;
    if (visible_rows < 1) visible_rows = 1;
    int total_rows = (count + cols - 1) / cols;
    int max_fm_scroll = total_rows > visible_rows ? (total_rows - visible_rows) * 80 : 0;
    if (fm_state.scroll > max_fm_scroll) fm_state.scroll = max_fm_scroll;

    for (int i = 0; i < count; i++) {
        vfs::VfsNode* child = vfs::get_node(children[i]);
        if (!child) continue;

        int col = i % cols;
        int row = i / cols;
        int ix = fx + col * 90;
        int iy = fy + row * 80 - fm_state.scroll;

        if (iy + 70 < y || iy > y + wh) continue;
        if (iy < y + 5 || iy + 70 > y + wh - 5) continue;

        bool hover = (i == fm_state.hover);
        bool sel = (i == fm_state.selected);

        if (hover || sel) {
            graphics::fill_rounded_rect(ix - 4, iy - 4, 84, 76, 8,
                sel ? Color(60, 60, 90) : Color(45, 45, 65));
        }

        if (child->type == vfs::NODE_DIR) {
            graphics::fill_rounded_rect(ix + 16, iy + 4, 48, 36, 4, theme::FOLDER);
            graphics::fill_rounded_rect(ix + 16, iy + 4, 20, 8, 3, theme::FOLDER);
        } else {
            graphics::fill_rounded_rect(ix + 22, iy + 4, 36, 42, 3, theme::WIN_BG);
            graphics::draw_rect(ix + 22, iy + 4, 36, 42, theme::FILE_CLR, 1);
            for (int l = 0; l < 4; l++) {
                graphics::fill_rect(ix + 28, iy + 14 + l * 7, 24, 2, theme::TEXT_DIM);
            }
        }

        int nw = font::text_width(child->name, 1);
        if (nw > 76) nw = 76;
        font::draw_string(ix + (80 - nw) / 2, iy + 50, child->name, theme::TEXT_W);
    }

    // Status bar
    int sb_y = y + wh - 24;
    graphics::fill_rect(x + sbw + 1, sb_y, ww - sbw - 1, 24, theme::SIDEBAR);
    graphics::fill_rect(x + sbw + 1, sb_y, ww - sbw - 1, 1, theme::WIN_BORDER);
    char items_str[32] = {};
    itoa_dec(count, items_str);
    int slen = str_len(items_str);
    items_str[slen++] = ' ';
    items_str[slen++] = 'i';
    items_str[slen++] = 't';
    items_str[slen++] = 'e';
    items_str[slen++] = 'm';
    items_str[slen++] = 's';
    items_str[slen] = 0;
    font::draw_string(x + sbw + 12, sb_y + 5, items_str, theme::TEXT_DIM);

    // Scrollbar
    if (max_fm_scroll > 0) {
        int sb_x = x + ww - 14;
        int sb_y2 = y + 8;
        int sb_h = wh - 24 - 16;
        if (sb_h < 8) sb_h = 8;
        graphics::fill_rounded_rect(sb_x, sb_y2, 8, sb_h, 4, Color(30, 30, 42));
        int thumb_h = sb_h * visible_rows / total_rows;
        if (thumb_h < 12) thumb_h = 12;
        int range = sb_h - thumb_h;
        int off = fm_state.scroll * range / max_fm_scroll;
        if (off < 0) off = 0;
        if (off > range) off = range;
        graphics::fill_rounded_rect(sb_x, sb_y2 + off, 8, thumb_h, 4, theme::ACCENT);
    }
}

static bool fm_handle_hover(gui::Window* w, int mx, int my) {
    int x = w->bounds.x;
    int y = w->bounds.y + theme::WIN_TITLE_H + 32;
    int ww = w->bounds.w;
    int sbw = 160;
    int fw_area = ww - sbw - 24;
    int cols = fw_area / 90;
    if (cols < 1) cols = 1;

    int fx = x + sbw + 12;
    int fy = y + 12;

    int children[32];
    int count = vfs::get_children(fm_state.current_dir, children, 32);
    fm_state.hover = -1;

    for (int i = 0; i < count; i++) {
        int col = i % cols;
        int row = i / cols;
        int ix = fx + col * 90 - 4;
        int iy = fy + row * 80 - fm_state.scroll - 4;
        if (mx >= ix && mx < ix + 84 && my >= iy && my < iy + 76) {
            fm_state.hover = i;
            return true;
        }
    }
    return false;
}

// ==================== TERMINAL (working) ====================
static char term_buf[256] = {};
static int term_len = 0;
static int term_cursor_blink = 0;
static int term_scroll = 0;
static bool term_sb_dragging = false;
static int term_sb_grab = 0;

static int term_total_lines() {
    int tl = 0;
    const char* o = shell::shell_state.output;
    while (*o) { if (*o == '\n') tl++; o++; }
    if (tl == 0) {
        if (shell::shell_state.output[0]) tl = 1;
    } else if (o[-1] != '\n') {
        tl++;
    }
    return tl;
}

static int term_max_lines(gui::Window* w) {
    int wh = w->bounds.h - theme::WIN_TITLE_H;
    int ml = (wh - 40) / 14;
    if (ml < 1) ml = 1;
    return ml;
}

static int term_max_scroll(gui::Window* w) {
    int ms = term_total_lines() - term_max_lines(w);
    return ms < 0 ? 0 : ms;
}

static void term_sb_geometry(gui::Window* w, int* track_x, int* track_y, int* track_h) {
    int x = w->bounds.x + 1;
    int y = w->bounds.y + theme::WIN_TITLE_H;
    int ww = w->bounds.w - 2;
    int wh = w->bounds.h - theme::WIN_TITLE_H;
    int input_y = y + wh - 28;
    *track_x = x + ww - 14;
    *track_y = y + 8;
    *track_h = input_y - y - 16;
    if (*track_h < 8) *track_h = 8;
}

static void term_draw(gui::Window* w) {
    int x = w->bounds.x + 1;
    int y = w->bounds.y + theme::WIN_TITLE_H;
    int ww = w->bounds.w - 2;
    int wh = w->bounds.h - theme::WIN_TITLE_H;

    graphics::fill_rect(x, y, ww, wh, Color(13, 13, 20));

    int line_y = y + 8;
    int line_h = 14;
    int max_lines = term_max_lines(w);

    int total_lines = term_total_lines();

    int start_line = 0;
    if (total_lines > max_lines) {
        start_line = total_lines - max_lines - term_scroll;
        if (start_line < 0) start_line = 0;
        if (start_line > total_lines - max_lines) start_line = total_lines - max_lines;
    }

    int drawn = 0;
    int current_line = 0;
    const char* out = shell::shell_state.output;
    const char* line_start = out;
    int lx = x + 10;
    int ly = line_y;

    while (*out && drawn < max_lines) {
        if (*out == '\n') {
            if (current_line >= start_line) {
                const char* p = line_start;
                int dx = lx;
                while (p < out && dx < x + ww - 10) {
                    if (*p == '\033' && *(p+1) == '[' && *(p+2) == '3' && *(p+3) == '4') {
                        p += 4;
                        while (p < out && *p != '\033') {
                            font::draw_char(dx, ly, *p, theme::FOLDER);
                            dx += 8;
                            p++;
                        }
                        if (*p == '\033' && *(p+1) == '[' && *(p+2) == '0' && *(p+3) == 'm') p += 4;
                    } else if (*p == '\033') {
                        while (p < out && *p != 'm' && *p != '\n') p++;
                        if (*p == 'm') p++;
                    } else {
                        font::draw_char(dx, ly, *p, theme::TEXT_W);
                        dx += 8;
                        p++;
                    }
                }
                ly += line_h;
                drawn++;
            }
            current_line++;
            line_start = out + 1;
        }
        out++;
    }
    if (drawn < max_lines && line_start < out) {
        const char* p = line_start;
        int dx = lx;
        while (p < out && dx < x + ww - 10) {
            font::draw_char(dx, ly, *p, theme::TEXT_W);
            dx += 8;
            p++;
        }
    }

    int input_y = y + wh - 28;
    graphics::fill_rect(x, input_y, ww, 28, Color(18, 18, 28));
    graphics::fill_rect(x, input_y, ww, 1, Color(40, 40, 55));
    font::draw_string(x + 10, input_y + 6, shell::shell_state.cwd, theme::BTN_MAXIMIZE);
    int prompt_w = font::text_width(shell::shell_state.cwd) + 8;
    font::draw_string(x + 10 + prompt_w, input_y + 6, "$ ", theme::TEXT_DIM);
    prompt_w += 16;
    font::draw_string(x + 10 + prompt_w, input_y + 6, term_buf, theme::TEXT_W);

    term_cursor_blink++;
    if ((term_cursor_blink / 30) % 2 == 0) {
        graphics::fill_rect(x + 10 + prompt_w + term_len * 8, input_y + 6, 8, 14, theme::ACCENT);
    }

    // Scrollbar
    int max_scroll = total_lines - max_lines;
    if (max_scroll > 0) {
        int sb_x, sb_y, sb_h;
        term_sb_geometry(w, &sb_x, &sb_y, &sb_h);
        graphics::fill_rounded_rect(sb_x, sb_y, 8, sb_h, 4, Color(30, 30, 42));
        int thumb_h = sb_h * max_lines / total_lines;
        if (thumb_h < 12) thumb_h = 12;
        int range = sb_h - thumb_h;
        int off = (max_scroll - term_scroll) * range / max_scroll;
        if (off < 0) off = 0;
        if (off > range) off = range;
        graphics::fill_rounded_rect(sb_x, sb_y + off, 8, thumb_h, 4,
            (term_sb_dragging || term_scroll > 0) ? theme::ACCENT : Color(70, 70, 95));
    }
}

// ==================== KERNEL heap init (forward decls) ====================
namespace kernel_heap {
    inline uint64_t get_used();
    inline uint64_t get_size();
}

// ==================== SYSTEM MONITOR ====================
static int mon_tick = 0;

static void mon_draw(gui::Window* w) {
    int x = w->bounds.x + 16;
    int y = w->bounds.y + theme::WIN_TITLE_H + 16;

    font::draw_string(x, y, "System Information", theme::TEXT_W, 1);
    y += 24;
    graphics::fill_rect(x, y, w->bounds.w - 32, 1, theme::WIN_BORDER);
    y += 10;

    font::draw_string(x, y, "OS:       Foxiwium OS v1.7.3", theme::TEXT_DIM); y += 18;
    font::draw_string(x, y, "Arch:     x86_64 (long mode)", theme::TEXT_DIM); y += 18;
    font::draw_string(x, y, "Kernel:   0.0-1-generic", theme::TEXT_DIM); y += 18;
    font::draw_string(x, y, "Theme:    KDE Breeze Dark", theme::TEXT_DIM); y += 24;

    font::draw_string(x, y, "CPU Usage:", theme::TEXT_W); y += 18;
    graphics::fill_rect(x, y, 200, 10, theme::SIDEBAR);
    int cpu_w = 30 + (mon_tick % 40);
    graphics::fill_rect(x, y, cpu_w, 10, theme::ACCENT);
    y += 20;

    font::draw_string(x, y, "Memory:", theme::TEXT_W); y += 18;
    graphics::fill_rect(x, y, 200, 10, theme::SIDEBAR);
    uint64_t total_mem = pmm::get_total_pages();
    uint64_t used_mem = pmm::get_used_pages();
    int mem_bar = total_mem > 0 ? (int)(used_mem * 200 / total_mem) : 100;
    graphics::fill_rect(x, y, mem_bar, 10, theme::BTN_MAXIMIZE);
    y += 20;

    char memstr[64] = "Used: ";
    int sl = 6;
    itoa_dec((int)(used_mem * 4 / 1024), memstr + sl);
    while(memstr[sl]) sl++;
    memstr[sl++] = '/';
    itoa_dec((int)(total_mem * 4 / 1024), memstr + sl);
    while(memstr[sl]) sl++;
    memstr[sl++] = 'M';
    memstr[sl] = 'B';
    memstr[++sl] = 0;
    font::draw_string(x, y, memstr, theme::TEXT_DIM); y += 18;

    // Uptime
    char ut[64] = "Uptime: ";
    int s = mon_tick / 100;
    int sl2 = 8;
    itoa_dec(s, ut + sl2);
    while(ut[sl2]) sl2++;
    ut[sl2++] = 's';
    ut[sl2] = 0;
    font::draw_string(x, y, ut, theme::TEXT_DIM); y += 18;

    mon_tick++;

    // Processes
    char procstr[64] = "Processes: ";
    int ps2 = 11;
    itoa_dec(pcb.count, procstr + ps2);
    while(procstr[ps2]) ps2++;
    procstr[ps2++] = ' ';
    procstr[ps2++] = 'a';
    procstr[ps2++] = 'c';
    procstr[ps2++] = 't';
    procstr[ps2++] = 'i';
    procstr[ps2++] = 'v';
    procstr[ps2++] = 'e';
    procstr[ps2] = 0;
    font::draw_string(x, y, procstr, theme::TEXT_DIM); y += 24;

    // VFS info
    font::draw_string(x, y, "Virtual FS:", theme::TEXT_W); y += 18;
    char vfs_str[64] = "Nodes: ";
    int vi = 7;
    itoa_dec(vfs::get_node_count(), vfs_str + vi);
    while(vfs_str[vi]) vi++;
    font::draw_string(x, y, vfs_str, theme::TEXT_DIM); y += 18;

    char heap_str[64] = "Heap: ";
    int hi = 6;
    itoa_dec((int)(kernel_heap::get_used() / 1024), heap_str + hi);
    while(heap_str[hi]) hi++;
    heap_str[hi++] = '/';
    itoa_dec((int)(kernel_heap::get_size() / 1024), heap_str + hi);
    while(heap_str[hi]) hi++;
    heap_str[hi++] = 'K';
    heap_str[hi] = 'B';
    heap_str[++hi] = 0;
    font::draw_string(x, y, heap_str, theme::TEXT_DIM);
}

// ==================== KERNEL heap init ====================

namespace kernel_heap {
    struct HeapBlock {
        HeapBlock* next;
        uint64_t size;
        bool free;
    };

    static uint64_t heap_base_addr = 0;
    static uint64_t heap_total = 0;
    static uint64_t heap_used = 0;
    static HeapBlock* head = nullptr;

    inline void init(uint64_t start = 0xFFFF800040000000ULL, uint64_t size = 0x200000) {
        uint64_t pages = (size + 4095) / 4096;
        for (uint64_t i = 0; i < pages; i++) {
            void* phys = pmm::alloc_page();
            if (!phys) break;
            vmm::map_page((void*)(start + i * 4096), phys,
                           vmm::PAGE_PRESENT | vmm::PAGE_WRITE);
            heap_total += 4096;
        }
        heap_base_addr = start;

        head = (HeapBlock*)start;
        head->next = nullptr;
        head->size = heap_total - sizeof(HeapBlock);
        head->free = true;
        heap_used = sizeof(HeapBlock);
    }

    inline void* alloc(uint64_t size) {
        if (size == 0) return nullptr;
        size = (size + 15) & ~15ULL;

        HeapBlock* best = nullptr;
        HeapBlock* cur = head;
        HeapBlock* prev = nullptr;

        while (cur) {
            if (cur->free && cur->size >= size) {
                if (!best || cur->size < best->size) {
                    best = cur;
                }
            }
            prev = cur;
            cur = cur->next;
        }
        (void)prev;

        if (!best) {
            uint64_t needed = size + sizeof(HeapBlock) + 4096;
            uint64_t pages = (needed + 4095) / 4096;
            for (uint64_t i = 0; i < pages; i++) {
                void* phys = pmm::alloc_page();
                if (!phys) return nullptr;
                vmm::map_page((void*)(heap_base_addr + heap_total + i * 4096), phys,
                               vmm::PAGE_PRESENT | vmm::PAGE_WRITE);
                heap_total += 4096;
            }

            best = (HeapBlock*)(heap_base_addr + heap_total - pages * 4096);
            best->next = nullptr;
            best->size = pages * 4096 - sizeof(HeapBlock);
            best->free = true;

            HeapBlock* last = head;
            if (!last) {
                head = best;
            } else {
                while (last->next) last = last->next;
                last->next = best;
            }
        }

        if (best->size > size + sizeof(HeapBlock) + 64) {
            HeapBlock* split = (HeapBlock*)((uint64_t)best + sizeof(HeapBlock) + size);
            split->next = best->next;
            split->size = best->size - size - sizeof(HeapBlock);
            split->free = true;
            best->next = split;
            best->size = size;
        }

        best->free = false;
        heap_used += best->size + sizeof(HeapBlock);
        return (void*)((uint64_t)best + sizeof(HeapBlock));
    }

    inline void free(void* ptr) {
        if (!ptr) return;
        HeapBlock* block = (HeapBlock*)((uint64_t)ptr - sizeof(HeapBlock));
        block->free = true;
        heap_used -= block->size + sizeof(HeapBlock);

        HeapBlock* cur = head;
        while (cur) {
            if (cur->free && cur->next && cur->next->free) {
                cur->size += sizeof(HeapBlock) + cur->next->size;
                cur->next = cur->next->next;
                continue;
            }
            cur = cur->next;
        }
    }

    inline uint64_t get_used() { return heap_used; }
    inline uint64_t get_size() { return heap_total; }
}

void* operator new(uint64_t size) { return kernel_heap::alloc(size); }
void* operator new[](uint64_t size) { return kernel_heap::alloc(size); }
void operator delete(void* ptr) { kernel_heap::free(ptr); }
void operator delete[](void* ptr) { kernel_heap::free(ptr); }
void operator delete(void* ptr, uint64_t) { kernel_heap::free(ptr); }
void operator delete[](void* ptr, uint64_t) { kernel_heap::free(ptr); }

// ==================== MAIN ====================
// Staging buffer for the initramfs (lives in kernel BSS, safe from PMM
// reuse; the multiboot module itself may be loaded into memory that the
// kernel later allocates for heap/page tables).
static uint8_t initramfs_buffer[256 * 1024];

extern "C" void __attribute__((noreturn)) kernel_main64(uint32_t mb2_magic, void* mb2_info_ptr) {
    extern char __bss_start[];
    extern char __bss_end[];
    for (char* p = __bss_start; p < __bss_end; p++) *p = 0;

    serial_init();
    serial_print("[FOXIWIUM] booting v0.1...\n");
    debug_print("[FOXIWIUM] booting v0.1...\n");

    idt::init();
    debug_print("[FOXIWIUM] idt init\n");

    if (mb2_magic == MB2_MAGIC) {
        framebuffer::init((Multiboot2Info*)mb2_info_ptr);

        // Load modules (the initramfs is passed as a multiboot2 module)
        uint8_t* ptr = (uint8_t*)mb2_info_ptr + 8;
        while (true) {
            Multiboot2Tag* tag = (Multiboot2Tag*)ptr;
            if (tag->type == MB2_TAG_END) break;

            if (tag->type == MB2_TAG_MODULE) {
                Multiboot2TagModule* mod = (Multiboot2TagModule*)tag;
                uint32_t mlen = mod->mod_end - mod->mod_start;
                if (mlen > sizeof(initramfs_buffer)) mlen = sizeof(initramfs_buffer);
                const uint8_t* src = (const uint8_t*)(uint64_t)mod->mod_start;
                for (uint32_t i = 0; i < mlen; i++) initramfs_buffer[i] = src[i];
                initramfs::init(initramfs_buffer, mlen);
                debug_print("[FOXIWIUM] initramfs module loaded (");
                char numbuf[16];
                int ni = 0;
                uint32_t msize = mod->mod_end - mod->mod_start;
                if (msize == 0) { numbuf[ni++] = '0'; }
                while (msize > 0) { numbuf[ni++] = '0' + msize % 10; msize /= 10; }
                while (ni > 0) debug_putchar(numbuf[--ni]);
                debug_print(" bytes, ");
                ni = 0;
                int nentries = initramfs::get_entry_count();
                if (nentries == 0) { numbuf[ni++] = '0'; }
                while (nentries > 0) { numbuf[ni++] = '0' + nentries % 10; nentries /= 10; }
                while (ni > 0) debug_putchar(numbuf[--ni]);
                debug_print(" entries)\n");
            }

            uint32_t sz = tag->size;
            if (sz < 8) sz = 8;
            ptr += (sz + 7) & ~7;
        }
    }
    debug_print("[FOXIWIUM] framebuffer parsed\n");

    vmm::init();
    debug_print("[FOXIWIUM] vmm init\n");

    uint64_t total_mem = 512 * 1024 * 1024;
    pmm::init(total_mem);
    debug_print("[FOXIWIUM] pmm init\n");

    uint64_t kernel_stack_top;
    asm volatile("mov %%rsp, %0" : "=r"(kernel_stack_top));
    // Полная инициализация GDT (ядро + user-сегменты 0x23/0x2B + TSS):
    // boot.asm загрузил лишь минимальную GDT без user-дескрипторов.
    gdt::init(kernel_stack_top);
    debug_print("[FOXIWIUM] gdt+tss init\n");

    kernel_heap::init();
    debug_print("[FOXIWIUM] kernel heap init\n");

    proc::init();
    debug_print("[FOXIWIUM] proc init\n");

    pit::init(100);
    debug_print("[FOXIWIUM] pit init\n");

    uint64_t* syscall_stack = (uint64_t*)pmm::alloc_page();
    cpu_data.kernel_stack = syscall_stack ? (uint64_t)syscall_stack + 4096 : kernel_stack_top;
    syscall::write_msr(MSR_KERNEL_GS_BASE, (uint64_t)&cpu_data);
    syscall::write_msr(MSR_GS_BASE, 0);
    debug_print("[FOXIWIUM] GS base set\n");

    syscall::write_msr(MSR_LSTAR, (uint64_t)syscall_entry);
    syscall::write_msr(MSR_STAR, ((uint64_t)0x08 << 32) | ((uint64_t)0x18 << 48));
    syscall::write_msr(MSR_FMASK, 0x200);
    uint64_t efer = syscall::read_msr(MSR_EFER);
    syscall::write_msr(MSR_EFER, efer | 1);
    debug_print("[FOXIWIUM] syscall init\n");

    syscalls::init();
    debug_print("[FOXIWIUM] syscall handlers registered\n");

    pic::unmask_irq(0);
    pic::unmask_irq(1);
    pic::unmask_irq(2);
    mouse::init();
    pic::unmask_irq(12);

    if (net::init()) {
        debug_print("[FOXIWIUM] RTL8139 NIC up\n");
    } else {
        debug_print("[FOXIWIUM] no NIC found\n");
    }

    framebuffer::clear(0xFF292D33);
    cache_wallpaper();
    draw_boot_screen();
    boot_pause_ms(5000);
    debug_print("[FOXIWIUM] framebuffer ready\n");

    // Init VFS
    vfs::init();
    debug_print("[FOXIWIUM] vfs init\n");

    // Mount initramfs content into the VFS
    vfs::mount_initramfs();
    debug_print("[FOXIWIUM] initramfs mounted\n");

    // Init shell
    shell::init();
    debug_print("[FOXIWIUM] shell init\n");

    // Init apps
    calc::init();
    editor::init();
    settings::init();
    fm_init();
    debug_print("[FOXIWIUM] apps init\n");

    // Create windows
    desktop.init();

    gui::Window* fm_win = desktop.create_window(80, 60, 700, 480, "Files");
    fm_win->on_draw = fm_draw;
    fm_win->visible = false;
    app_list[APP_FILES].window_idx = desktop.window_count - 1;

    gui::Window* term_win = desktop.create_window(140, 120, 560, 360, "Terminal");
    term_win->on_draw = term_draw;
    term_win->visible = false;
    app_list[APP_TERMINAL].window_idx = desktop.window_count - 1;

    gui::Window* mon_win = desktop.create_window(200, 80, 420, 380, "System Monitor");
    mon_win->on_draw = mon_draw;
    mon_win->visible = false;
    app_list[APP_MONITOR].window_idx = desktop.window_count - 1;

    gui::Window* calc_win = desktop.create_window(400, 100, 320, 350, "Calculator");
    calc_win->on_draw = calc::draw;
    calc_win->active = false;
    calc_win->visible = false;
    app_list[APP_CALCULATOR].window_idx = desktop.window_count - 1;

    gui::Window* editor_win = desktop.create_window(120, 80, 640, 480, "Text Editor");
    editor_win->on_draw = editor::draw;
    editor_win->active = false;
    editor_win->visible = false;
    app_list[APP_EDITOR].window_idx = desktop.window_count - 1;

    gui::Window* settings_win = desktop.create_window(160, 60, 560, 420, "Settings");
    settings_win->on_draw = settings::draw;
    settings_win->active = false;
    settings_win->visible = false;
    app_list[APP_SETTINGS].window_idx = desktop.window_count - 1;

    gui::Window* about_win = desktop.create_window(500, 100, 400, 480, "About");
    about_win->on_draw = about::draw;
    about_win->active = false;
    about_win->visible = false;
    app_list[APP_ABOUT].window_idx = desktop.window_count - 1;

    gui::Window* browser_win = desktop.create_window(180, 60, 760, 540, "Browser");
    browser_win->on_draw = browser::draw;
    browser_win->active = false;
    browser_win->visible = false;
    app_list[APP_BROWSER].window_idx = desktop.window_count - 1;
    browser::init();
    debug_print("[BROWSER] init ok\n");

    debug_print("[FOXIWIUM] windows created\n");

    int shell_pid = proc::create_process("shell", 0x400000, true);
    if (shell_pid > 0) {
        Process& shell_proc = pcb.processes[shell_pid - 1];
        // Процессу доступно несколько страниц кода/данных начиная с 0x400000
        // (пользовательский бинарь Xandr больше одной страницы).
        uint32_t total = shell_binary_len;
        uint32_t npages = (total + 4095) / 4096;
        if (npages < 1) npages = 1;
        for (uint32_t pg = 0; pg < npages; pg++) {
            void* code_page = pmm::alloc_page();
            if (!code_page) break;
            uint8_t* dst = (uint8_t*)code_page;
            uint32_t base = pg * 4096;
            uint32_t chunk = (total > base) ? (total - base) : 0;
            if (chunk > 4096) chunk = 4096;
            for (uint32_t i = 0; i < chunk; i++) dst[i] = shell_binary[base + i];
            for (uint32_t i = chunk; i < 4096; i++) dst[i] = 0;
            proc::map_user_page(shell_proc.page_table,
                0x400000 + (uint64_t)pg * 4096, (uint64_t)code_page,
                vmm::PAGE_PRESENT | vmm::PAGE_WRITE | vmm::PAGE_USER);
        }
    }
    debug_print("[FOXIWIUM] shell process created\n");

    asm volatile("sti");
    debug_print("[FOXIWIUM] interrupts enabled\n");

    // Запустить пользовательский процесс (Xandr): он рисует кадр, шлёт его в
    // debug-порт (syscall write -> 0xE9) и завершается. Управление вернётся
    // сюда через exit_process -> context_switch, после чего продолжается
    // основной цикл рабочего стола.
    if (shell_pid > 0) {
        pcb.current = shell_pid - 1;
        proc::run_user_process(pcb.processes[shell_pid - 1]);
        asm volatile("sti");
        debug_print("[FOXIWIUM] user process finished\n");
    }

    int tick = 0;
    bool dirty = true;
    int prev_cursor_x = -100, prev_cursor_y = -100;
    static constexpr int CURSOR_SAVE_SIZE = 20;
    uint32_t cursor_save[CURSOR_SAVE_SIZE * CURSOR_SAVE_SIZE];
    bool prev_right_pressed = false;

    while (true) {
        net::poll();
        if (browser::update()) dirty = true;
        bool had_input = false;
        int sc;
        while ((sc = keyboard::get_scancode()) != -1) {
            if (!(sc & 0x80)) {
                had_input = true;
                char c = keyboard::scancode_to_ascii(sc);

                // Launcher keyboard input
                if (launcher_open) {
                    handle_launcher_key(c);
                }

                // Terminal input
                if (term_win && term_win->focused && term_win->visible && !launcher_open && !ctx_menu_open) {
                    uint8_t kcode = sc & 0x7F;
                    if (kcode == 0x49) {
                        int ms = term_max_scroll(term_win);
                        term_scroll += term_max_lines(term_win);
                        if (term_scroll > ms) term_scroll = ms;
                        debug_print("[TERM] pgup scroll=");
                        char db[16]; int dv = term_scroll; int di2 = 0; if (dv == 0) db[di2++]='0';
                        while (dv > 0) { db[di2++] = '0' + dv % 10; dv /= 10; } db[di2] = 0;
                        for (int k = 0; k < di2/2; k++) { char t=db[k]; db[k]=db[di2-1-k]; db[di2-1-k]=t; }
                        debug_print(db); debug_print("\n");
                    } else if (kcode == 0x51) {
                        term_scroll -= term_max_lines(term_win);
                        if (term_scroll < 0) term_scroll = 0;
                    } else if (c == '\b' && term_len > 0) {
                        term_len--;
                        term_buf[term_len] = 0;
                    } else if (c == '\n') {
                        shell::execute(term_buf);
                        term_len = 0;
                        term_buf[0] = 0;
                        term_scroll = 0;
                    } else if (c && term_len < 255) {
                        term_buf[term_len++] = c;
                        term_buf[term_len] = 0;
                    }
                }

                // Editor input
                if (editor_win && editor_win->focused && editor_win->visible && !launcher_open && !ctx_menu_open) {
                    editor::handle_key(sc, c);
                }

                // Browser input
                if (browser_win && browser_win->focused && browser_win->visible && !launcher_open && !ctx_menu_open) {
                    browser::handle_key(sc, c);
                }
            }
        }

        // === SLEEP MODE (display off, wake on any input) ===
        if (system_sleeping) {
            framebuffer::clear(0xFF000000);
            font::draw_string_centered(framebuffer::get_width() / 2,
                framebuffer::get_height() / 2 - 8,
                "Sleeping - press any key or move the mouse", theme::TEXT_DIM);
            font::draw_string_centered(framebuffer::get_width() / 2,
                framebuffer::get_height() / 2 + 16,
                "Foxiwium OS", theme::TEXT_W, 2);
            framebuffer::flip();

            int sleep_x = mouse::cursor_x;
            int sleep_y = mouse::cursor_y;
            while (system_sleeping) {
                while (keyboard::get_scancode() != -1) system_sleeping = false;
                if (mouse::cursor_x != sleep_x || mouse::cursor_y != sleep_y) system_sleeping = false;
                if (mouse::left_pressed || mouse::right_pressed) system_sleeping = false;
                pit::sleep(16);
            }

            // Drop any input buffered during sleep so it won't fire stale actions
            while (keyboard::get_scancode() != -1) {}
            mouse::left_pressed_prev = mouse::left_pressed;
            prev_right_pressed = mouse::right_pressed;
            mouse::scroll_delta = 0;

            dirty = true;
            continue;
        }

        int mx = mouse::cursor_x;
        int my = mouse::cursor_y;
        bool mleft = mouse::left_pressed;
        bool mright = mouse::right_pressed;

        if (mx != prev_cursor_x || my != prev_cursor_y) dirty = true;

        uint32_t fw = framebuffer::get_width();
        uint32_t fh = framebuffer::get_height();
        int panel_y = fh - theme::PANEL_H;

        // === LAUNCHER MENU INTERACTION ===
        if (mleft && !mouse::left_pressed_prev && launcher_open) {
            had_input = true;
            int menu_w = 420;
            int menu_h = 360;
            int menu_x = 0;
            int menu_y = fh - theme::PANEL_H - menu_h;
            int sbw = 100;

            if (mx >= menu_x && mx < menu_x + menu_w && my >= menu_y && my < menu_y + menu_h) {
                // Check sidebar categories
                if (mx < menu_x + sbw) {
                    int cat = (my - menu_y - 12) / 28;
                    if (cat >= 0 && cat < 4) launcher_cat_hover = cat;
                } else {
                    // Check app items
                    int ax = menu_x + sbw + 8;
                    int ay = menu_y + 12;
                    int item_h = 32;
                    for (int i = 0; i < APP_COUNT; i++) {
                        int iy = ay + i * item_h;
                        if (iy + item_h > menu_y + menu_h - 44) break;
                        if (mx >= ax - 4 && mx < menu_x + menu_w && my >= iy && my < iy + item_h) {
                            // Launch app
                            int wi = app_list[i].window_idx;
                            if (wi >= 0 && wi < desktop.window_count && desktop.windows[wi]) {
                                gui::Window* win = desktop.windows[wi];
                                win->visible = true;
                                win->active = true;
                                win->focused = true;
                                app_list[i].running = true;
                                desktop.active_window = wi;
                                for (int j = 0; j < desktop.window_count; j++) {
                                    if (desktop.windows[j]) desktop.windows[j]->focused = (j == wi);
                                }
                            }
                            launcher_open = false;
                            launcher_search_len = 0;
                            launcher_search[0] = 0;
                            dirty = true;
                            break;
                        }
                    }
                }
            } else {
                // Click outside launcher -> close
                launcher_open = false;
                launcher_search_len = 0;
                launcher_search[0] = 0;
                dirty = true;
            }
        }

        // === CONTEXT MENU INTERACTION ===
        if (mleft && !mouse::left_pressed_prev && ctx_menu_open) {
            had_input = true;
            int mw = 180;
            int mh = 88;
            int item_h = 28;
            int cmx = mx;
            int cmy = my;
            if (cmx + mw > (int)fw) cmx = fw - mw;
            if (cmy + mh > panel_y) cmy = panel_y - mh;

            if (mx >= cmx && mx < cmx + mw && my >= cmy && my < cmy + mh) {
                int item = (my - cmy - 4) / item_h;
                if (item >= 0 && item < 3) {
                    if (item == 0) {
                        // "Configure Desktop" - open settings
                        int wi = app_list[APP_SETTINGS].window_idx;
                        if (wi >= 0 && wi < desktop.window_count && desktop.windows[wi]) {
                            gui::Window* win = desktop.windows[wi];
                            win->visible = true;
                            win->active = true;
                            win->focused = true;
                            app_list[APP_SETTINGS].running = true;
                            desktop.active_window = wi;
                            for (int j = 0; j < desktop.window_count; j++) {
                                if (desktop.windows[j]) desktop.windows[j]->focused = (j == wi);
                            }
                        }
                    } else if (item == 1) {
                        // "Open Terminal"
                        int wi = app_list[APP_TERMINAL].window_idx;
                        if (wi >= 0 && wi < desktop.window_count && desktop.windows[wi]) {
                            gui::Window* win = desktop.windows[wi];
                            win->visible = true;
                            win->active = true;
                            win->focused = true;
                            app_list[APP_TERMINAL].running = true;
                            desktop.active_window = wi;
                            for (int j = 0; j < desktop.window_count; j++) {
                                if (desktop.windows[j]) desktop.windows[j]->focused = (j == wi);
                            }
                        }
                    } else if (item == 2) {
                        // "Leave" -> open power menu
                        power_menu_open = true;
                        power_menu_hover = -1;
                    }
                }
            }
            ctx_menu_open = false;
            dirty = true;
        }

        // === POWER MENU INTERACTION ===
        if (mleft && !mouse::left_pressed_prev && power_menu_open) {
            had_input = true;
            int mw = 190;
            int item_h = 42;
            int mh = 8 + 3 * item_h + 8;
            int pmx = fw - mw - 8;
            int pmy = fh - theme::PANEL_H - mh - 8;

            if (mx >= pmx && mx < pmx + mw && my >= pmy && my < pmy + mh) {
                int item = (my - pmy - 8) / item_h;
                if (item >= 0 && item < 3) {
                    if (item == 0) {
                        system_sleeping = true;      // sleep
                    } else if (item == 1) {
                        power::reboot();             // restart
                    } else {
                        power::shutdown();           // power off
                    }
                }
            }
            power_menu_open = false;
            dirty = true;
        }

        // === PANEL INTERACTION ===
        bool in_panel = (my >= panel_y && my < (int)fh);

        // Panel launcher button click
        if (mleft && !mouse::left_pressed_prev && in_panel && mx < 56) {
            had_input = true;
            launcher_open = !launcher_open;
            ctx_menu_open = false;
            power_menu_open = false;
            launcher_search_len = 0;
            launcher_search[0] = 0;
            dirty = true;
        }

        // Power button click
        int pbtn_s = 32;
        int pbtn_x = fw - 98;
        if (mleft && !mouse::left_pressed_prev && in_panel && mx >= pbtn_x && mx < pbtn_x + pbtn_s) {
            had_input = true;
            power_menu_open = !power_menu_open;
            launcher_open = false;
            ctx_menu_open = false;
            launcher_search_len = 0;
            launcher_search[0] = 0;
            dirty = true;
        }

        // Panel task button clicks
        if (mleft && !mouse::left_pressed_prev && in_panel && mx >= 64) {
            had_input = true;
            int task_x = 64;
            for (int i = 0; i < APP_COUNT; i++) {
                if (!app_list[i].running) continue;
                if (app_list[i].window_idx < 0 || app_list[i].window_idx >= desktop.window_count) continue;

                int tw = font::text_width(app_list[i].label) + 24;
                if (mx >= task_x && mx < task_x + tw) {
                    int wi = app_list[i].window_idx;
                    if (desktop.active_window == wi) {
                        // Toggle minimize
                        gui::Window* win = desktop.windows[wi];
                        win->visible = !win->visible;
                        if (!win->visible) {
                            win->active = false;
                            app_list[i].running = false;
                        }
                    } else {
                        // Focus this window
                        gui::Window* win = desktop.windows[wi];
                        win->visible = true;
                        win->active = true;
                        win->focused = true;
                        desktop.active_window = wi;
                        for (int j = 0; j < desktop.window_count; j++) {
                            if (desktop.windows[j]) desktop.windows[j]->focused = (j == wi);
                        }
                    }
                    break;
                }
                task_x += tw + 4;
            }
            dirty = true;
        }

        // Panel hover detection for task buttons
        int old_panel_task_hover = panel_task_hover;
        panel_task_hover = -1;
        if (in_panel && mx >= 64) {
            int task_x = 64;
            int task_idx = 0;
            for (int i = 0; i < APP_COUNT; i++) {
                if (!app_list[i].running) continue;
                if (app_list[i].window_idx < 0 || app_list[i].window_idx >= desktop.window_count) continue;
                int tw = font::text_width(app_list[i].label) + 24;
                if (mx >= task_x && mx < task_x + tw) {
                    panel_task_hover = task_idx;
                    break;
                }
                task_x += tw + 4;
                task_idx++;
            }
        }
        if (panel_task_hover != old_panel_task_hover) dirty = true;

        // === LAUNCHER HOVER ===
        if (launcher_open) {
            int menu_w = 420;
            int menu_h = 360;
            int menu_x = 0;
            int menu_y = fh - theme::PANEL_H - menu_h;
            int sbw = 100;
            int old_launcher_hover = launcher_hover;
            int old_cat_hover = launcher_cat_hover;
            launcher_hover = -1;

            if (mx >= menu_x && mx < menu_x + menu_w && my >= menu_y && my < menu_y + menu_h) {
                if (mx < menu_x + sbw) {
                    int cat = (my - menu_y - 12) / 28;
                    if (cat >= 0 && cat < 4) launcher_cat_hover = cat;
                } else {
                    int ax = menu_x + sbw + 8;
                    int ay = menu_y + 12;
                    int item_h = 32;
                    for (int i = 0; i < APP_COUNT; i++) {
                        int iy = ay + i * item_h;
                        if (iy + item_h > menu_y + menu_h - 44) break;
                        if (mx >= ax - 4 && mx < menu_x + menu_w && my >= iy && my < iy + item_h) {
                            launcher_hover = i;
                            break;
                        }
                    }
                }
            }
            if (launcher_hover != old_launcher_hover || launcher_cat_hover != old_cat_hover) dirty = true;
        }

        // === CONTEXT MENU HOVER ===
        if (ctx_menu_open) {
            int mw = 180;
            int mh = 88;
            int item_h = 28;
            int cmx = mx;
            int cmy = my;
            if (cmx + mw > (int)fw) cmx = fw - mw;
            if (cmy + mh > panel_y) cmy = panel_y - mh;
            int old_ctx_hover = ctx_menu_hover;
            ctx_menu_hover = -1;
            if (mx >= cmx && mx < cmx + mw && my >= cmy && my < cmy + mh) {
                int item = (my - cmy - 4) / item_h;
                if (item >= 0 && item < 3) ctx_menu_hover = item;
            }
            if (ctx_menu_hover != old_ctx_hover) dirty = true;
        }

        // === POWER MENU HOVER ===
        if (power_menu_open) {
            int mw = 190;
            int item_h = 42;
            int mh = 8 + 3 * item_h + 8;
            int pmx = fw - mw - 8;
            int pmy = fh - theme::PANEL_H - mh - 8;
            int old_power_hover = power_menu_hover;
            power_menu_hover = -1;
            if (mx >= pmx && mx < pmx + mw && my >= pmy && my < pmy + mh) {
                int item = (my - pmy - 8) / item_h;
                if (item >= 0 && item < 3) power_menu_hover = item;
            }
            if (power_menu_hover != old_power_hover) dirty = true;
        }

        // === DESKTOP RIGHT-CLICK CONTEXT MENU ===
        if (mright && !prev_right_pressed && !in_panel) {
            had_input = true;
            bool over_window = false;
            for (int i = desktop.window_count - 1; i >= 0; i--) {
                if (desktop.windows[i] && desktop.windows[i]->visible &&
                    desktop.windows[i]->bounds.contains(mx, my)) {
                    over_window = true;
                    break;
                }
            }
            if (!over_window) {
                ctx_menu_open = !ctx_menu_open;
                launcher_open = false;
                power_menu_open = false;
                dirty = true;
            }
        }

        // Close launcher/context on left click outside
        if (mleft && !mouse::left_pressed_prev) {
            if (launcher_open && mx >= 420) {
                launcher_open = false;
                launcher_search_len = 0;
                launcher_search[0] = 0;
                dirty = true;
            }
            if (power_menu_open) {
                // Don't close when the click is on the power button itself
                bool on_power_btn = (in_panel && mx >= (int)fw - 98 && mx < (int)fw - 66);
                if (!on_power_btn) {
                    int mw = 190;
                    int item_h = 42;
                    int mh = 8 + 3 * item_h + 8;
                    int pmx = fw - mw - 8;
                    int pmy = fh - theme::PANEL_H - mh - 8;
                    if (mx < pmx || mx >= pmx + mw || my < pmy || my >= pmy + mh) {
                        power_menu_open = false;
                        dirty = true;
                    }
                }
            }
            if (ctx_menu_open) {
                int mw = 180;
                int mh = 88;
                int cmx = mx;
                int cmy = my;
                if (cmx + mw > (int)fw) cmx = fw - mw;
                if (cmy + mh > panel_y) cmy = panel_y - mh;
                if (mx < cmx || mx >= cmx + mw || my < cmy || my >= cmy + mh) {
                    ctx_menu_open = false;
                    dirty = true;
                }
            }
        }

        // Window dragging
        bool was_dragging = false;
        for (int i = 0; i < desktop.window_count; i++) {
            if (desktop.windows[i] && (desktop.windows[i]->dragging || desktop.windows[i]->resizing)) was_dragging = true;
        }

        if (!launcher_open && !ctx_menu_open) {
            desktop.handle_mouse(mx, my, mleft, mright);
        }

        bool is_dragging = false;
        for (int i = 0; i < desktop.window_count; i++) {
            if (desktop.windows[i] && (desktop.windows[i]->dragging || desktop.windows[i]->resizing)) is_dragging = true;
        }
        if (was_dragging || is_dragging) dirty = true;

        // === WINDOW SNAP (KWin-style edge snapping) ===
        if (!was_dragging && is_dragging && desktop.active_window >= 0) {
            gui::Window* win = desktop.windows[desktop.active_window];
            if (win && win->dragging) {
                int snap_margin = 12;
                int usable_h = fh - theme::PANEL_H;
                // Left half snap
                if (mx <= snap_margin) {
                    win->bounds.x = 0;
                    win->bounds.y = 0;
                    win->bounds.w = fw / 2;
                    win->bounds.h = usable_h;
                    win->title_bar = {win->bounds.x, win->bounds.y, win->bounds.w, gui::Window::TITLE_HEIGHT};
                    win->content = {win->bounds.x, win->bounds.y + gui::Window::TITLE_HEIGHT,
                                   win->bounds.w, win->bounds.h - gui::Window::TITLE_HEIGHT};
                }
                // Right half snap
                else if (mx >= (int)fw - snap_margin) {
                    win->bounds.x = fw / 2;
                    win->bounds.y = 0;
                    win->bounds.w = fw / 2;
                    win->bounds.h = usable_h;
                    win->title_bar = {win->bounds.x, win->bounds.y, win->bounds.w, gui::Window::TITLE_HEIGHT};
                    win->content = {win->bounds.x, win->bounds.y + gui::Window::TITLE_HEIGHT,
                                   win->bounds.w, win->bounds.h - gui::Window::TITLE_HEIGHT};
                }
                // Top edge = maximize
                else if (my <= snap_margin) {
                    win->bounds.x = 0;
                    win->bounds.y = 0;
                    win->bounds.w = fw;
                    win->bounds.h = usable_h;
                    win->title_bar = {win->bounds.x, win->bounds.y, win->bounds.w, gui::Window::TITLE_HEIGHT};
                    win->content = {win->bounds.x, win->bounds.y + gui::Window::TITLE_HEIGHT,
                                   win->bounds.w, win->bounds.h - gui::Window::TITLE_HEIGHT};
                }
            }
        }

        // Don't allow windows into the panel area
        for (int i = 0; i < desktop.window_count; i++) {
            if (desktop.windows[i] && desktop.windows[i]->visible) {
                gui::Window* w = desktop.windows[i];
                if (w->bounds.y + w->bounds.h > panel_y) {
                    w->bounds.h = panel_y - w->bounds.y;
                    if (w->bounds.h < w->resize_min_h) {
                        w->bounds.h = w->resize_min_h;
                    }
                }
            }
        }

        // Scroll
        int sd = mouse::scroll_delta;
        if (sd != 0) {
            had_input = true;
            mouse::scroll_delta = 0;
            if (term_win && term_win->active && term_win->visible &&
                mx >= term_win->bounds.x && mx < term_win->bounds.x + term_win->bounds.w &&
                my >= term_win->bounds.y && my < term_win->bounds.y + term_win->bounds.h) {
                term_scroll += sd * 3;
                if (term_scroll < 0) term_scroll = 0;
            } else if (fm_win && fm_win->active && fm_win->visible) {
                fm_state.scroll -= sd * 30;
                if (fm_state.scroll < 0) fm_state.scroll = 0;
            } else if (browser_win && browser_win->active && browser_win->visible) {
                browser::wheel(sd);
            }
        }

        // Browser mouse interaction
        if (browser_win && browser_win->active && browser_win->visible &&
            !launcher_open && !ctx_menu_open) {
            browser::mouse_update(browser_win, mx, my, mleft);
        }

        // Terminal scrollbar interaction
        if (term_sb_dragging && !mleft) term_sb_dragging = false;
        if (term_win && term_win->visible && term_win->active) {
            int sb_x, sb_y, sb_h;
            term_sb_geometry(term_win, &sb_x, &sb_y, &sb_h);
            int tms = term_max_scroll(term_win);
            int tml = term_max_lines(term_win);
            if (tms > 0 && mx >= sb_x && mx < sb_x + 8 && my >= sb_y && my < sb_y + sb_h) {
                if (mleft && !mouse::left_pressed_prev) {
                    int ttl = term_total_lines();
                    int thumb_h = sb_h * tml / ttl;
                    if (thumb_h < 12) thumb_h = 12;
                    int range = sb_h - thumb_h;
                    int off = (tms - term_scroll) * range / tms;
                    if (off < 0) off = 0;
                    if (off > range) off = range;
                    if (my >= sb_y + off && my < sb_y + off + thumb_h) {
                        term_sb_dragging = true;
                        term_sb_grab = my - (sb_y + off);
                        debug_print("[TERM] sb drag start\n");
                    } else if (my < sb_y + off) {
                        term_scroll += tml;
                        if (term_scroll > tms) term_scroll = tms;
                        debug_print("[TERM] sb pageup\n");
                    } else {
                        term_scroll -= tml;
                        if (term_scroll < 0) term_scroll = 0;
                        debug_print("[TERM] sb pagedown\n");
                    }
                    had_input = true;
                }
            }
            if (term_sb_dragging) {
                int ttl = term_total_lines();
                int thumb_h = sb_h * tml / ttl;
                if (thumb_h < 12) thumb_h = 12;
                int range = sb_h - thumb_h;
                if (range < 1) range = 1;
                int ny = my - term_sb_grab - sb_y;
                if (ny < 0) ny = 0;
                if (ny > range) ny = range;
                term_scroll = tms - ny * tms / range;
                if (term_scroll < 0) term_scroll = 0;
                if (term_scroll > tms) term_scroll = tms;
                had_input = true;
            }
        }

        // File Manager hover
        if (fm_win && fm_win->active && fm_win->visible) {
            fm_handle_hover(fm_win, mx, my);
        }

        // File Manager clicks (navigation, selection, sidebar, toolbar)
        if (mleft && !mouse::left_pressed_prev && fm_win && fm_win->active && fm_win->visible) {
            fm_handle_click(fm_win, mx, my);
            had_input = true;
        }

        // File Manager scrollbar interaction
        if (fm_state.sb_dragging && !mleft) fm_state.sb_dragging = false;
        if (fm_win && fm_win->visible && fm_win->active) {
            int fm_x = fm_win->bounds.x;
            int fm_y = fm_win->bounds.y + theme::WIN_TITLE_H + 32;
            int fm_ww = fm_win->bounds.w;
            int fm_wh = fm_win->bounds.h - theme::WIN_TITLE_H - 32 - 24;
            int fm_sbx = fm_x + fm_ww - 14;
            int fm_sby = fm_y + 8;
            int fm_sbh = fm_wh - 8 - 16;
            if (fm_sbh < 8) fm_sbh = 8;
            int fcols, fcount;
            { int fx2, fy2, fwa2; fm_grid_geom(fm_win, &fcols, &fx2, &fy2, &fwa2, &fcount); }
            int frows_v = (fm_wh - 24) / 80;
            if (frows_v < 1) frows_v = 1;
            int frows_t = (fcount + fcols - 1) / fcols;
            int fmax_s = frows_t > frows_v ? (frows_t - frows_v) * 80 : 0;
            if (fmax_s > 0) {
                int fthumb = fm_sbh * frows_v / frows_t;
                if (fthumb < 12) fthumb = 12;
                int frange = fm_sbh - fthumb;
                if (mleft && !mouse::left_pressed_prev && mx >= fm_sbx && mx < fm_sbx + 8 &&
                    my >= fm_sby && my < fm_sby + fm_sbh) {
                    int foff = fm_state.scroll * frange / fmax_s;
                    if (my >= fm_sby + foff && my < fm_sby + foff + fthumb) {
                        fm_state.sb_dragging = true;
                        fm_state.sb_grab = my - (fm_sby + foff);
                    } else if (my < fm_sby + foff) {
                        fm_state.scroll -= frows_v * 80;
                        if (fm_state.scroll < 0) fm_state.scroll = 0;
                    } else {
                        fm_state.scroll += frows_v * 80;
                        if (fm_state.scroll > fmax_s) fm_state.scroll = fmax_s;
                    }
                    had_input = true;
                }
                if (fm_state.sb_dragging) {
                    int fr = frange;
                    if (fr < 1) fr = 1;
                    int ny = my - fm_state.sb_grab - fm_sby;
                    if (ny < 0) ny = 0;
                    if (ny > fr) ny = fr;
                    fm_state.scroll = ny * fmax_s / fr;
                    if (fm_state.scroll > fmax_s) fm_state.scroll = fmax_s;
                    had_input = true;
                }
            }
        }

        // Calculator hover
        if (calc_win && calc_win->active && calc_win->visible) {
            calc::handle_hover(calc_win, mx, my);
        }

        // Settings hover
        if (settings_win && settings_win->active && settings_win->visible) {
            settings::handle_hover(settings_win, mx, my);
        }

        if (had_input) dirty = true;

        term_cursor_blink++;
        if (term_cursor_blink % 30 == 0) dirty = true;

        tick++;
        if (tick % 100 == 0) {
            g_min++;
            if (g_min >= 60) { g_min = 0; g_hour = (g_hour + 1) % 24; }
            dirty = true;
        }

        if (!dirty) {
            if (mx != prev_cursor_x || my != prev_cursor_y) {
                uint32_t pitch = framebuffer::get_pitch() / 4;
                uint32_t* buf = framebuffer::get_buffer();

                if (prev_cursor_x >= 0 && prev_cursor_y >= 0 &&
                    prev_cursor_x + CURSOR_SAVE_SIZE <= (int)fw && prev_cursor_y + CURSOR_SAVE_SIZE <= (int)fh) {
                    for (int cy = 0; cy < CURSOR_SAVE_SIZE; cy++)
                        for (int cx = 0; cx < CURSOR_SAVE_SIZE; cx++)
                            buf[(prev_cursor_y + cy) * pitch + (prev_cursor_x + cx)] = cursor_save[cy * CURSOR_SAVE_SIZE + cx];
                }

                if (mx >= 0 && my >= 0 && mx + CURSOR_SAVE_SIZE <= (int)fw && my + CURSOR_SAVE_SIZE <= (int)fh) {
                    for (int cy = 0; cy < CURSOR_SAVE_SIZE; cy++)
                        for (int cx = 0; cx < CURSOR_SAVE_SIZE; cx++)
                            cursor_save[cy * CURSOR_SAVE_SIZE + cx] = buf[(my + cy) * pitch + (mx + cx)];
                }

                int cs = 14;
                for (int i = 0; i < cs; i++) {
                    graphics::put_pixel(mx + 1, my + i, theme::TEXT_W);
                    graphics::put_pixel(mx, my + i, theme::TEXT_W);
                    if (i < cs - 2) graphics::put_pixel(mx + 2 + i, my + i + 1, theme::TEXT_W);
                }
                for (int i = 0; i < cs; i++) graphics::put_pixel(mx, my + i, Color(0, 0, 0));
                graphics::put_pixel(mx + cs + 1, my, Color(0, 0, 0));

                framebuffer::flip();
                prev_cursor_x = mx;
                prev_cursor_y = my;
            }
            pit::sleep(16);
            continue;
        }

        dirty = false;

        draw_wallpaper();

        // Draw windows bottom-up; active window on top
        for (int i = 0; i < desktop.window_count; i++) {
            if (i == desktop.active_window) continue;
            draw_window(desktop.windows[i]);
        }
        if (desktop.active_window >= 0) draw_window(desktop.windows[desktop.active_window]);

        draw_panel();

        if (power_menu_open) draw_power_menu();
        if (launcher_open) draw_launcher();
        if (ctx_menu_open) draw_ctx_menu();

        {
            int cs = 14;
            for (int i = 0; i < cs; i++) {
                graphics::put_pixel(mx + 1, my + i, theme::TEXT_W);
                graphics::put_pixel(mx, my + i, theme::TEXT_W);
                if (i < cs - 2) graphics::put_pixel(mx + 2 + i, my + i + 1, theme::TEXT_W);
            }
            for (int i = 0; i < cs; i++) graphics::put_pixel(mx, my + i, Color(0, 0, 0));
            graphics::put_pixel(mx + cs + 1, my, Color(0, 0, 0));
        }

        prev_cursor_x = mx;
        prev_cursor_y = my;
        prev_right_pressed = mright;

        framebuffer::flip();
        pit::sleep(16);
    }
}
