#include "injector.h"

#include "arch_x86_64.h"
#include "controller.h"

#include <errno.h>
#include <stdint.h>
#include <string.h>

int lp_x86_64_make_rel_jmp(uint64_t from_addr, uint64_t to_addr, unsigned char out[5])
{
    int64_t rel = (int64_t)to_addr - (int64_t)(from_addr + LP_X86_64_JMP_REL32_SIZE);
    if (rel < INT32_MIN || rel > INT32_MAX) {
        errno = ERANGE;
        return -1;
    }

    out[0] = 0xE9;
    int32_t rel32 = (int32_t)rel;
    memcpy(&out[1], &rel32, sizeof(rel32));
    return 0;
}

int lp_x86_64_read_original_code(pid_t pid, struct probe_desc *desc, int min_len)
{
    if (min_len < LP_X86_64_JMP_REL32_SIZE) {
        min_len = LP_X86_64_JMP_REL32_SIZE;
    }
    if (min_len > LP_MAX_ORIGINAL_CODE) {
        errno = EINVAL;
        return -1;
    }

    desc->original_len = min_len;
    return lp_remote_read(pid, desc->target_addr, desc->original_code,
                          (size_t)desc->original_len);
}

int lp_x86_64_patch_entry(pid_t pid, const struct probe_desc *desc)
{
    unsigned char patch[LP_X86_64_JMP_REL32_SIZE];

    if (desc->original_len < LP_X86_64_JMP_REL32_SIZE) {
        errno = EINVAL;
        return -1;
    }

    if (lp_x86_64_make_rel_jmp(desc->target_addr, desc->entry_stub_addr, patch) < 0) {
        return -1;
    }

    return lp_remote_write(pid, desc->target_addr, patch, sizeof(patch));
}

int lp_x86_64_restore_entry(pid_t pid, const struct probe_desc *desc)
{
    if (desc->original_len <= 0 || desc->original_len > LP_MAX_ORIGINAL_CODE) {
        errno = EINVAL;
        return -1;
    }

    return lp_remote_write(pid, desc->target_addr, desc->original_code,
                           (size_t)desc->original_len);
}
