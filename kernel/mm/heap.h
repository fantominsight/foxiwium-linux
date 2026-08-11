#pragma once
#include <stdint.h>
#include <stddef.h>
#include "pmm.h"
#include "vmm.h"

namespace heap {

struct Block {
    Block* next;
    uint64_t size;
    bool free;
};

static Block* head = nullptr;
static void* heap_start = nullptr;
static uint64_t heap_size = 0;
static uint64_t heap_used = 0;

inline void init(uint64_t start_addr = 0xFFFF800040000000ULL, uint64_t initial_size = 0x100000) {
    heap_start = (void*)start_addr;
    heap_size = 0;

    uint64_t pages_needed = (initial_size + 4095) / 4096;
    for (uint64_t i = 0; i < pages_needed; i++) {
        void* phys = pmm::alloc_page();
        if (!phys) break;
        vmm::map_page((void*)(start_addr + i * 4096), phys,
                       vmm::PAGE_PRESENT | vmm::PAGE_WRITE);
        heap_size += 4096;
    }

    head = (Block*)heap_start;
    head->next = nullptr;
    head->size = heap_size - sizeof(Block);
    head->free = true;
    heap_used = sizeof(Block);
}

inline void* alloc(uint64_t size) {
    if (size == 0) return nullptr;
    size = (size + 15) & ~15ULL;

    Block* best = nullptr;
    Block* best_prev = nullptr;
    Block* cur = head;
    Block* prev = nullptr;

    while (cur) {
        if (cur->free && cur->size >= size) {
            if (!best || cur->size < best->size) {
                best = cur;
                best_prev = prev;
            }
        }
        prev = cur;
        cur = cur->next;
    }

    if (!best) {
        uint64_t needed = size + sizeof(Block);
        uint64_t pages = (needed + 4095) / 4096;
        uint64_t old_pages = heap_size / 4096;

        for (uint64_t i = 0; i < pages; i++) {
            void* phys = pmm::alloc_page();
            if (!phys) return nullptr;
            vmm::map_page((void*)((uint64_t)heap_start + heap_size + i * 4096), phys,
                           vmm::PAGE_PRESENT | vmm::PAGE_WRITE);
            heap_size += 4096;
        }
        (void)old_pages;

        best = (Block*)((uint64_t)heap_start + heap_size - pages * 4096);
        best->next = nullptr;
        best->size = pages * 4096 - sizeof(Block);
        best->free = true;

        Block* last = head;
        if (!last) {
            head = best;
        } else {
            while (last->next) last = last->next;
            last->next = best;
        }
    }

    if (best->size > size + sizeof(Block) + 64) {
        Block* split = (Block*)((uint64_t)best + sizeof(Block) + size);
        split->next = best->next;
        split->size = best->size - size - sizeof(Block);
        split->free = true;
        best->next = split;
        best->size = size;
    }

    best->free = false;
    heap_used += best->size + sizeof(Block);
    return (void*)((uint64_t)best + sizeof(Block));
}

inline void free(void* ptr) {
    if (!ptr) return;

    Block* block = (Block*)((uint64_t)ptr - sizeof(Block));
    block->free = true;
    heap_used -= block->size + sizeof(Block);

    Block* cur = head;
    while (cur) {
        if (cur->free && cur->next && cur->next->free) {
            cur->size += sizeof(Block) + cur->next->size;
            cur->next = cur->next->next;
            continue;
        }
        cur = cur->next;
    }
}

inline uint64_t get_used() { return heap_used; }
inline uint64_t get_size() { return heap_size; }

}
