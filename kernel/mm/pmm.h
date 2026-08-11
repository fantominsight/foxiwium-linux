#pragma once
#include <stdint.h>
#include <stddef.h>

extern char __kernel_end[];

namespace pmm {

inline uint64_t* bitmap = nullptr;
inline uint64_t total_pages = 0;
inline uint64_t used_pages = 0;
inline uint64_t bitmap_size = 0;

inline void set_page(uint64_t page) {
    bitmap[page / 64] |= (1ULL << (page % 64));
}

inline void clear_page(uint64_t page) {
    bitmap[page / 64] &= ~(1ULL << (page % 64));
}

inline bool get_page(uint64_t page) {
    return bitmap[page / 64] & (1ULL << (page % 64));
}

inline void mark_used(uint64_t page) {
    if (!get_page(page)) {
        set_page(page);
        used_pages++;
    }
}

inline void mark_free(uint64_t page) {
    if (get_page(page)) {
        clear_page(page);
        used_pages--;
    }
}

inline void init(uint64_t mem_end) {
    total_pages = mem_end / 4096;
    bitmap_size = (total_pages + 63) / 64;
    bitmap = (uint64_t*)__kernel_end;

    uint64_t* end_of_bitmap = bitmap + bitmap_size;
    uint64_t kernel_end_addr = (uint64_t)end_of_bitmap;
    kernel_end_addr = (kernel_end_addr + 4095) & ~4095ULL;
    (void)kernel_end_addr;

    for (uint64_t i = 0; i < bitmap_size; i++)
        bitmap[i] = 0;

    for (uint64_t i = 0; i < total_pages; i++) {
        uint64_t addr = i * 4096;
        if (addr < 0x100000) { set_page(i); continue; }
        if (addr < (uint64_t)end_of_bitmap) { set_page(i); continue; }
        if (addr >= (uint64_t)__kernel_end &&
            addr < (uint64_t)end_of_bitmap + bitmap_size * 8) {
            set_page(i); continue;
        }
    }

    used_pages = 0;
    for (uint64_t i = 0; i < bitmap_size; i++) {
        uint64_t w = bitmap[i];
        for (int b = 0; b < 64; b++) {
            if (w & (1ULL << b)) used_pages++;
        }
    }
}

inline void* alloc_page() {
    for (uint64_t i = 0; i < bitmap_size; i++) {
        if (bitmap[i] != 0xFFFFFFFFFFFFFFFF) {
            for (int b = 0; b < 64; b++) {
                if (!(bitmap[i] & (1ULL << b))) {
                    uint64_t page = i * 64 + b;
                    if (page >= total_pages) return nullptr;
                    set_page(page);
                    used_pages++;
                    void* ptr = (void*)(page * 4096);
                    for (int j = 0; j < 4096; j++)
                        ((uint8_t*)ptr)[j] = 0;
                    return ptr;
                }
            }
        }
    }
    return nullptr;
}

inline void* alloc_pages(uint64_t count) {
    if (count == 0) return nullptr;
    if (count == 1) return alloc_page();

    for (uint64_t i = 0; i < total_pages - count + 1; i++) {
        bool found = true;
        for (uint64_t j = 0; j < count; j++) {
            if (get_page(i + j)) { found = false; break; }
        }
        if (found) {
            for (uint64_t j = 0; j < count; j++) {
                set_page(i + j);
                used_pages++;
            }
            void* ptr = (void*)(i * 4096);
            for (uint64_t j = 0; j < count * 4096; j++)
                ((uint8_t*)ptr)[j] = 0;
            return ptr;
        }
    }
    return nullptr;
}

inline void free_page(void* addr) {
    uint64_t page = (uint64_t)addr / 4096;
    if (page < total_pages && get_page(page)) {
        clear_page(page);
        used_pages--;
    }
}

inline void free_pages(void* addr, uint64_t count) {
    for (uint64_t i = 0; i < count; i++) {
        free_page((void*)((uint64_t)addr + i * 4096));
    }
}

inline uint64_t get_total_pages() { return total_pages; }
inline uint64_t get_used_pages() { return used_pages; }
inline uint64_t get_free_pages() { return total_pages - used_pages; }
inline uint64_t get_total_mb() { return total_pages * 4 / 1024; }
inline uint64_t get_used_mb() { return used_pages * 4 / 1024; }

}
