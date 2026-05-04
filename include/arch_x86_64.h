#ifndef LIGHTPROBE_ARCH_X86_64_H
#define LIGHTPROBE_ARCH_X86_64_H

#include <stdint.h>

#define LP_X86_64_JMP_REL32_SIZE 5
#define LP_X86_64_ABS_JMP_SIZE 12

struct lp_x86_64_regs_snapshot {
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rbp;
    uint64_t rbx;
    uint64_t rdx;
    uint64_t rax;
    uint64_t rcx;
    uint64_t rsp;
    uint64_t rflags;
};

static inline void lp_x86_64_collect_args(const struct lp_x86_64_regs_snapshot *regs,
                                          uint64_t args[6])
{
    args[0] = regs->rdi;
    args[1] = regs->rsi;
    args[2] = regs->rdx;
    args[3] = regs->rcx;
    args[4] = regs->r8;
    args[5] = regs->r9;
}

#endif

