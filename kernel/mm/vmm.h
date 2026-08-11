#pragma once
#include <stdint.h>
#include "pmm.h"

namespace vmm {

constexpr uint64_t PAGE_PRESENT  = 0x01;
constexpr uint64_t PAGE_WRITE    = 0x02;
constexpr uint64_t PAGE_USER     = 0x04;
constexpr uint64_t PAGE_DIRTY    = 0x20;
constexpr uint64_t PAGE_SIZE_2MB = 0x80;

typedef uint64_t pml4e_t;
typedef uint64_t pdpte_t;
typedef uint64_t pde_t;
typedef uint64_t pte_t;

static pml4e_t* kernel_pml4 = nullptr;
static pml4e_t* current_pml4 = nullptr;

inline uint64_t virt_to_phys(void* virt) {
    uint64_t va = (uint64_t)virt;
    pml4e_t* pml4 = (pml4e_t*)current_pml4;

    uint64_t pml4_idx = (va >> 39) & 0x1FF;
    uint64_t pdpt_idx = (va >> 30) & 0x1FF;
    uint64_t pd_idx   = (va >> 21) & 0x1FF;
    uint64_t pt_idx   = (va >> 12) & 0x1FF;

    if (!(pml4[pml4_idx] & PAGE_PRESENT)) return 0;
    pdpte_t* pdpt = (pdpte_t*)(pml4[pml4_idx] & ~0xFFFULL);

    if (pdpt[pdpt_idx] & PAGE_SIZE_2MB) {
        return (pdpt[pdpt_idx] & ~0x1FFFFFULL) | (va & 0x1FFFFF);
    }

    if (!(pdpt[pdpt_idx] & PAGE_PRESENT)) return 0;
    pde_t* pd = (pde_t*)(pdpt[pdpt_idx] & ~0xFFFULL);

    if (pd[pd_idx] & PAGE_SIZE_2MB) {
        return (pd[pd_idx] & ~0x1FFFFFULL) | (va & 0x1FFFFF);
    }

    if (!(pd[pd_idx] & PAGE_PRESENT)) return 0;
    pte_t* pt = (pte_t*)(pd[pd_idx] & ~0xFFFULL);

    if (!(pt[pt_idx] & PAGE_PRESENT)) return 0;
    return (pt[pt_idx] & ~0xFFFULL) | (va & 0xFFF);
}

inline void flush_tlb() {
    uint64_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    asm volatile("mov %0, %%cr3" :: "r"(cr3) : "memory");
}

inline void flush_tlb_page(uint64_t addr) {
    asm volatile("invlpg (%0)" :: "r"(addr) : "memory");
}

inline void map_page(void* virt, void* phys, uint64_t flags = PAGE_PRESENT | PAGE_WRITE) {
    uint64_t va = (uint64_t)virt;
    uint64_t pa = (uint64_t)phys;

    pml4e_t* pml4 = (pml4e_t*)current_pml4;
    uint64_t pml4_idx = (va >> 39) & 0x1FF;
    uint64_t pdpt_idx = (va >> 30) & 0x1FF;
    uint64_t pd_idx   = (va >> 21) & 0x1FF;
    uint64_t pt_idx   = (va >> 12) & 0x1FF;

    if (!(pml4[pml4_idx] & PAGE_PRESENT)) {
        void* new_pdpt = pmm::alloc_page();
        pml4[pml4_idx] = (uint64_t)new_pdpt | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    }
    pdpte_t* pdpt = (pdpte_t*)(pml4[pml4_idx] & ~0xFFFULL);

    if (!(pdpt[pdpt_idx] & PAGE_PRESENT)) {
        void* new_pd = pmm::alloc_page();
        pdpt[pdpt_idx] = (uint64_t)new_pd | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    }
    pde_t* pd = (pde_t*)(pdpt[pdpt_idx] & ~0xFFFULL);

    if (pd[pd_idx] & PAGE_SIZE_2MB) {
        uint64_t big_phys = pd[pd_idx] & ~0x1FFFFFULL;
        uint64_t big_virt = va & ~0x1FFFFFULL;
        void* new_pt = pmm::alloc_page();
        pte_t* new_pt_ptr = (pte_t*)new_pt;
        for (int i = 0; i < 512; i++) {
            new_pt_ptr[i] = (big_phys + i * 4096) | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
        }
        pd[pd_idx] = (uint64_t)new_pt | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
        (void)big_virt;
        flush_tlb();
        pte_t* pt = (pte_t*)(pd[pd_idx] & ~0xFFFULL);
        pt[pt_idx] = (pa & ~0xFFFULL) | flags;
        flush_tlb_page(va);
        return;
    }

    if (!(pd[pd_idx] & PAGE_PRESENT)) {
        void* new_pt = pmm::alloc_page();
        pd[pd_idx] = (uint64_t)new_pt | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    }
    pte_t* pt = (pte_t*)(pd[pd_idx] & ~0xFFFULL);

    pt[pt_idx] = (pa & ~0xFFFULL) | flags;
    flush_tlb_page(va);
}

inline void unmap_page(void* virt) {
    uint64_t va = (uint64_t)virt;
    pml4e_t* pml4 = (pml4e_t*)current_pml4;

    uint64_t pml4_idx = (va >> 39) & 0x1FF;
    uint64_t pdpt_idx = (va >> 30) & 0x1FF;
    uint64_t pd_idx   = (va >> 21) & 0x1FF;
    uint64_t pt_idx   = (va >> 12) & 0x1FF;

    if (!(pml4[pml4_idx] & PAGE_PRESENT)) return;
    pdpte_t* pdpt = (pdpte_t*)(pml4[pml4_idx] & ~0xFFFULL);
    if (!(pdpt[pdpt_idx] & PAGE_PRESENT)) return;
    pde_t* pd = (pde_t*)(pdpt[pdpt_idx] & ~0xFFFULL);
    if (!(pd[pd_idx] & PAGE_PRESENT)) return;
    pte_t* pt = (pte_t*)(pd[pd_idx] & ~0xFFFULL);

    pt[pt_idx] = 0;
    flush_tlb_page(va);
}

inline void switch_pml4(void* pml4_phys) {
    current_pml4 = (pml4e_t*)pml4_phys;
    asm volatile("mov %0, %%cr3" :: "r"((uint64_t)pml4_phys) : "memory");
}

inline pml4e_t* get_kernel_pml4() { return kernel_pml4; }

inline pml4e_t* create_address_space() {
    pml4e_t* new_pml4 = (pml4e_t*)pmm::alloc_page();
    pml4e_t* kernel = kernel_pml4;

    for (int i = 0; i < 256; i++) new_pml4[i] = 0;
    for (int i = 256; i < 512; i++) new_pml4[i] = 0;

    // Глубокая копия нижней половины таблиц ядра с установкой USER-бита на
    // всех уровнях: без него любой доступ из CPL3 вызывает защитный #PF,
    // даже если сам PTE имеет USER. Верхняя половина (heap ядра) остаётся
    // недоступной пользователю.
    for (int i = 0; i < 256; i++) {
        uint64_t e = kernel[i];
        if (!(e & PAGE_PRESENT)) continue;

        pdpte_t* new_pdpt = (pdpte_t*)pmm::alloc_page();
        pdpte_t* src_pdpt = (pdpte_t*)(e & ~0xFFFULL);

        for (int j = 0; j < 512; j++) {
            uint64_t se = src_pdpt[j];
            if (!(se & PAGE_PRESENT)) continue;

            if (se & PAGE_SIZE_2MB) {
                new_pdpt[j] = (se & ~0xFFFULL) | (se & 0xFFF) | PAGE_USER;
                continue;
            }

            pde_t* new_pd = (pde_t*)pmm::alloc_page();
            pde_t* src_pd = (pde_t*)(se & ~0xFFFULL);

            for (int k = 0; k < 512; k++) {
                uint64_t ke = src_pd[k];
                if (!(ke & PAGE_PRESENT)) continue;

                if (ke & PAGE_SIZE_2MB) {
                    new_pd[k] = (ke & ~0xFFFULL) | (ke & 0xFFF) | PAGE_USER;
                    continue;
                }

                pte_t* new_pt = (pte_t*)pmm::alloc_page();
                pte_t* src_pt = (pte_t*)(ke & ~0xFFFULL);

                for (int l = 0; l < 512; l++) {
                    uint64_t pe = src_pt[l];
                    if (!(pe & PAGE_PRESENT)) continue;
                    new_pt[l] = (pe & ~0xFFFULL) | (pe & 0xFFF) | PAGE_USER;
                }

                new_pd[k] = (uint64_t)new_pt | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
            }

            new_pdpt[j] = (uint64_t)new_pd | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
        }

        new_pml4[i] = (uint64_t)new_pdpt | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    }

    return new_pml4;
}

inline void init() {
    uint64_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    kernel_pml4 = (pml4e_t*)cr3;
    current_pml4 = kernel_pml4;
}

}
