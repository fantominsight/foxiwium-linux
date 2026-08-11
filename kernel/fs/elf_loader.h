#pragma once
#include <stdint.h>
#include "../mm/pmm.h"
#include "../mm/vmm.h"
#include "../proc/process.h"

#define EI_NIDENT 16

struct Elf64_Ehdr {
    uint8_t  e_ident[EI_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

struct Elf64_Phdr {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
};

#define PT_NULL    0
#define PT_LOAD    1
#define PT_DYNAMIC 2
#define PT_INTERP  3

#define PF_X 0x1
#define PF_W 0x2
#define PF_R 0x4

namespace elf {

inline bool load(const uint8_t* data, uint64_t size, const char* name, bool user = true) {
    if (size < sizeof(Elf64_Ehdr)) return false;

    const Elf64_Ehdr* ehdr = (const Elf64_Ehdr*)data;
    if (ehdr->e_ident[0] != 0x7F || ehdr->e_ident[1] != 'E' ||
        ehdr->e_ident[2] != 'L' || ehdr->e_ident[3] != 'F') {
        return false;
    }
    if (ehdr->e_ident[4] != 2) return false;

    int pid = proc::create_process(name, ehdr->e_entry, user);
    if (pid < 0) return false;

    Process* p = &pcb.processes[pid - 1];

    const Elf64_Phdr* phdr = (const Elf64_Phdr*)(data + ehdr->e_phoff);
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type != PT_LOAD) continue;

        uint64_t vaddr = phdr[i].p_vaddr;
        uint64_t filesz = phdr[i].p_filesz;
        uint64_t memsz = phdr[i].p_memsz;
        uint64_t offset = phdr[i].p_offset;

        uint64_t num_pages = (memsz + 4095) / 4096;
        for (uint64_t pg = 0; pg < num_pages; pg++) {
            void* phys = pmm::alloc_page();
            if (!phys) return false;

            vmm::switch_pml4(p->page_table);
            uint64_t page_vaddr = (vaddr & ~0xFFFULL) + pg * 4096;
            uint64_t flags = vmm::PAGE_PRESENT | vmm::PAGE_USER;
            if (phdr[i].p_flags & PF_W) flags |= vmm::PAGE_WRITE;
            vmm::map_page((void*)page_vaddr, phys, flags);
            vmm::switch_pml4(vmm::get_kernel_pml4());

            uint64_t copy_start = pg * 4096;
            uint64_t file_start = offset + copy_start;

            uint64_t phys_start = (uint64_t)phys;
            uint64_t to_copy = 0;
            if (file_start < size && copy_start < filesz) {
                to_copy = filesz - copy_start;
                if (to_copy > 4096) to_copy = 4096;
                __builtin_memcpy((void*)phys_start, data + file_start, to_copy);
            }
        }
    }

    return true;
}

}
