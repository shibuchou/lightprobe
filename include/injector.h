#ifndef LIGHTPROBE_INJECTOR_H
#define LIGHTPROBE_INJECTOR_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "probe_types.h"

int lp_x86_64_make_rel_jmp(uint64_t from_addr, uint64_t to_addr, unsigned char out[5]);
int lp_x86_64_read_original_code(pid_t pid, struct probe_desc *desc, int min_len);
int lp_x86_64_patch_entry(pid_t pid, const struct probe_desc *desc);
int lp_x86_64_restore_entry(pid_t pid, const struct probe_desc *desc);
int lp_x86_64_build_trampoline(const struct probe_desc *desc,
                               uint64_t resume_addr,
                               unsigned char *buf,
                               size_t buf_len,
                               size_t *written);
int lp_x86_64_build_entry_stub(const struct probe_desc *desc,
                               unsigned char *buf,
                               size_t buf_len,
                               size_t *written);
int lp_x86_64_build_ret_stub(const struct probe_desc *desc,
                             unsigned char *buf,
                             size_t buf_len,
                             size_t *written);

#endif
