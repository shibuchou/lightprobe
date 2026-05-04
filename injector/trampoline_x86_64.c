#include "injector.h"

#include "arch_x86_64.h"

#include <errno.h>
#include <string.h>

int lp_x86_64_build_trampoline(const struct probe_desc *desc,
                               uint64_t resume_addr,
                               unsigned char *buf,
                               size_t buf_len,
                               size_t *written)
{
    size_t need = (size_t)desc->original_len + LP_X86_64_ABS_JMP_SIZE;
    if (buf_len < need) {
        errno = ENOSPC;
        return -1;
    }

    memcpy(buf, desc->original_code, (size_t)desc->original_len);

    unsigned char *jmp = buf + desc->original_len;
    jmp[0] = 0x48;
    jmp[1] = 0xB8;
    memcpy(jmp + 2, &resume_addr, sizeof(resume_addr));
    jmp[10] = 0xFF;
    jmp[11] = 0xE0;

    if (written != NULL) {
        *written = need;
    }
    return 0;
}

