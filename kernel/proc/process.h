#pragma once
#include <stdint.h>
#include "../mm/vmm.h"
#include "../mm/pmm.h"
#include "../arch/gdt.h"

#define MAX_PROCESSES 64
#define MAX_FD 16
#define PROCESS_STACK_SIZE 0x40000

enum ProcessState {
    PROC_UNUSED,
    PROC_READY,
    PROC_RUNNING,
    PROC_WAITING,
    PROC_ZOMBIE,
};

struct TrapFrame {
    uint64_t r15, r14, r13, r12;
    uint64_t r11, r10, r9, r8;
    uint64_t rdi, rsi, rbp, rdx, rcx, rbx, rax;
    uint64_t int_no, err_code;
    uint64_t rip, cs, rflags, rsp, ss;
};

struct Process {
    int pid;
    ProcessState state;
    char name[32];

    uint64_t* kernel_stack;
    uint64_t* user_stack;
    uint64_t rsp;
    uint64_t rip;

    uint64_t* page_table;
    uint64_t kernel_stack_top;

    uint64_t wait_target;

    int exit_code;
    bool killed;
};

struct ProcessControlBlock {
    Process processes[MAX_PROCESSES];
    int current;
    int count;
};

extern ProcessControlBlock pcb;

extern "C" void context_switch(uint64_t* old_rsp, uint64_t new_rsp);
extern "C" void enter_user_mode(uint64_t* resume_rsp, uint64_t frame_rsp);

// Точка возврата в основной цикл ядра после завершения user-процесса.
static uint64_t g_resume_rsp = 0;

namespace proc {

inline Process* get_current() {
    if (pcb.current < 0 || pcb.current >= MAX_PROCESSES) return nullptr;
    return &pcb.processes[pcb.current];
}

inline int create_process(const char* name, uint64_t entry, bool user = false) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (pcb.processes[i].state == PROC_UNUSED) {
            Process& p = pcb.processes[i];
            for (uint64_t j = 0; j < sizeof(Process); j++)
                ((uint8_t*)&p)[j] = 0;
            p.pid = i + 1;
            p.state = PROC_READY;
            int len = 0;
            while (name[len] && len < 31) { p.name[len] = name[len]; len++; }
            p.name[len] = 0;
            p.exit_code = 0;
            p.killed = false;

            p.kernel_stack = (uint64_t*)pmm::alloc_page();
            if (!p.kernel_stack) return -1;
            p.kernel_stack_top = (uint64_t)p.kernel_stack + 4096;

            p.user_stack = (uint64_t*)pmm::alloc_page();
            if (!p.user_stack) { pmm::free_page(p.kernel_stack); return -1; }

            if (user) {
                p.page_table = vmm::create_address_space();
                uint64_t user_stack_phys = (uint64_t)vmm::virt_to_phys(p.user_stack);
                vmm::switch_pml4(p.page_table);
                vmm::map_page((void*)(USER_RSP - 0x1000), (void*)user_stack_phys,
                               vmm::PAGE_PRESENT | vmm::PAGE_WRITE | vmm::PAGE_USER);

                p.rip = entry;
                uint64_t stack_top = USER_RSP - 8;   // iretq-фрейм: 40 байт, чтобы
                                                     // вершина стека была 16-выровнена

                // iretq-фрейм пишется прямо в страницу стека пользователя,
                // пока активен его page table (в kernel-таблице этот адрес
                // не замаплен).
                uint64_t* sp = (uint64_t*)stack_top;
                *(--sp) = 0x23;          // ss  (USER_DATA 0x20 | RPL3)
                *(--sp) = stack_top;     // user rsp
                *(--sp) = 0x202;         // rflags: IF | reserved
                *(--sp) = 0x2B;          // cs  (USER_CODE 0x28 | RPL3)
                *(--sp) = entry;         // rip

                p.rsp = (uint64_t)sp;
                vmm::switch_pml4(vmm::get_kernel_pml4());
            } else {
                p.page_table = vmm::get_kernel_pml4();

                uint64_t* sp = (uint64_t*)((uint64_t)p.kernel_stack + 4096);
                *(--sp) = 0x10;
                *(--sp) = p.kernel_stack_top;
                *(--sp) = 0x200246;
                *(--sp) = 0x08;
                *(--sp) = entry;
                *(--sp) = 0;
                *(--sp) = 0;
                *(--sp) = 0;
                *(--sp) = 0;
                *(--sp) = 0;
                *(--sp) = 0;
                *(--sp) = 0;
                *(--sp) = 0;
                *(--sp) = 0;
                *(--sp) = 0;
                *(--sp) = 0;
                *(--sp) = 0;

                p.rsp = (uint64_t)sp;
            }

            pcb.count++;
            return p.pid;
        }
    }
    return -1;
}

inline void map_user_page(uint64_t* page_table, uint64_t virt, uint64_t phys, uint64_t flags) {
    uint64_t saved_cr3;
    asm volatile("mov %%cr3, %0" : "=r"(saved_cr3));
    vmm::switch_pml4(page_table);
    vmm::map_page((void*)virt, (void*)phys, flags);
    vmm::switch_pml4((void*)saved_cr3);
}

// Запустить user-процесс: переключить адресное пространство и войти в CPL3.
// Возвращается (из enter_user_mode) после exit_process.
inline void run_user_process(Process& p) {
    gdt::set_kernel_stack(p.kernel_stack_top);
    vmm::switch_pml4(p.page_table);
    enter_user_mode(&g_resume_rsp, p.rsp);
}

inline void exit_process(int code) {
    Process* p = get_current();
    if (!p) return;
    p->state = PROC_ZOMBIE;
    p->exit_code = code;
    p->killed = true;
    asm volatile("cli");
    uint64_t* old = &p->rsp;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (pcb.processes[i].state == PROC_READY ||
            pcb.processes[i].state == PROC_RUNNING) {
            if (i != pcb.current) {
                pcb.current = i;
                pcb.processes[i].state = PROC_RUNNING;
                uint64_t* new_rsp = &pcb.processes[i].rsp;
                vmm::switch_pml4(pcb.processes[i].page_table);
                context_switch(old, *new_rsp);
                return;
            }
        }
    }
    // Других процессов нет: вернуться в основной цикл ядра (если задана
    // точка возобновления), иначе остановить CPU.
    if (g_resume_rsp) {
        pcb.current = -1;
        vmm::switch_pml4(vmm::get_kernel_pml4());
        context_switch(old, g_resume_rsp);
        return;
    }
    while(true) asm volatile("hlt");
}

inline void schedule() {
    int start = pcb.current;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        int idx = (start + 1 + i) % MAX_PROCESSES;
        if (pcb.processes[idx].state == PROC_READY) {
            uint64_t* old = nullptr;
            if (pcb.current >= 0 && pcb.current < MAX_PROCESSES &&
                pcb.processes[pcb.current].state == PROC_RUNNING) {
                pcb.processes[pcb.current].state = PROC_READY;
                old = &pcb.processes[pcb.current].rsp;
            }
            pcb.current = idx;
            pcb.processes[idx].state = PROC_RUNNING;
            uint64_t* new_rsp = &pcb.processes[idx].rsp;
            vmm::switch_pml4(pcb.processes[idx].page_table);
            if (old) context_switch(old, *new_rsp);
            return;
        }
    }
}

inline void init() {
    for (uint64_t i = 0; i < sizeof(ProcessControlBlock); i++)
        ((uint8_t*)&pcb)[i] = 0;
    pcb.current = -1;
}

}
