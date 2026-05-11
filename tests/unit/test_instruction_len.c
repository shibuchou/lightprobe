#include "injector.h"

#include <stdio.h>

static int expect_len(const unsigned char *code, size_t code_len, size_t expected)
{
    size_t actual = 0;
    if (lp_x86_64_insn_len(code, code_len, &actual) < 0) {
        perror("lp_x86_64_insn_len");
        return -1;
    }
    if (actual != expected) {
        fprintf(stderr, "expected len=%lu actual=%lu\n", expected, actual);
        return -1;
    }
    return 0;
}

static int expect_patch_len(const unsigned char *code,
                            size_t code_len,
                            size_t min_len,
                            size_t expected)
{
    size_t actual = 0;
    if (lp_x86_64_calc_patch_len(code, code_len, min_len, &actual) < 0) {
        perror("lp_x86_64_calc_patch_len");
        return -1;
    }
    if (actual != expected) {
        fprintf(stderr, "expected patch_len=%lu actual=%lu\n", expected, actual);
        return -1;
    }
    return 0;
}

int main(void)
{
    const unsigned char push_rbp[] = {0x55};
    const unsigned char mov_rbp_rsp[] = {0x48, 0x89, 0xE5};
    const unsigned char sub_rsp_imm8[] = {0x48, 0x83, 0xEC, 0x20};
    const unsigned char mov_rax_riprel[] = {0x48, 0x8B, 0x05, 0x11, 0x22, 0x33, 0x44};
    const unsigned char call_rel32[] = {0xE8, 0x01, 0x00, 0x00, 0x00};
    const unsigned char prologue[] = {
        0x55,
        0x48, 0x89, 0xE5,
        0x48, 0x83, 0xEC, 0x20,
    };

    if (expect_len(push_rbp, sizeof(push_rbp), 1) < 0 ||
        expect_len(mov_rbp_rsp, sizeof(mov_rbp_rsp), 3) < 0 ||
        expect_len(sub_rsp_imm8, sizeof(sub_rsp_imm8), 4) < 0 ||
        expect_len(mov_rax_riprel, sizeof(mov_rax_riprel), 7) < 0 ||
        expect_len(call_rel32, sizeof(call_rel32), 5) < 0 ||
        expect_patch_len(prologue, sizeof(prologue), 5, 8) < 0) {
        return 1;
    }

    puts("instruction length tests passed");
    return 0;
}
