#include "injector.h"

#include "arch_x86_64.h"
#include "event.h"
#include "runtime.h"

#include <errno.h>
#include <stddef.h>
#include <string.h>

struct code_writer {
    unsigned char *buf;
    size_t cap;
    size_t len;
};

static int emit_bytes(struct code_writer *w, const unsigned char *bytes, size_t len)
{
    if (w->len + len > w->cap) {
        errno = ENOSPC;
        return -1;
    }
    memcpy(w->buf + w->len, bytes, len);
    w->len += len;
    return 0;
}

static int emit_u32(struct code_writer *w, uint32_t value)
{
    return emit_bytes(w, (const unsigned char *)&value, sizeof(value));
}

static int emit_u64(struct code_writer *w, uint64_t value)
{
    return emit_bytes(w, (const unsigned char *)&value, sizeof(value));
}

static int emit_movabs_r10(struct code_writer *w, uint64_t value)
{
    static const unsigned char op[] = {0x49, 0xBA};
    return emit_bytes(w, op, sizeof(op)) < 0 || emit_u64(w, value) < 0 ? -1 : 0;
}

static int emit_abs_jmp_r11_preserve(struct code_writer *w, uint64_t target)
{
    static const unsigned char head[] = {
        0x41, 0x53,             /* push r11 */
        0x49, 0xBB,             /* movabs r11, imm64 */
    };
    static const unsigned char tail[] = {
        0x4C, 0x87, 0x1C, 0x24, /* xchg [rsp], r11 */
        0xC3,                   /* ret */
    };

    if (emit_bytes(w, head, sizeof(head)) < 0 ||
        emit_u64(w, target) < 0 ||
        emit_bytes(w, tail, sizeof(tail)) < 0) {
        return -1;
    }
    return 0;
}

static int emit_mov_rax_from_rsp_disp8(struct code_writer *w, unsigned char disp)
{
    const unsigned char op[] = {0x48, 0x8B, 0x44, 0x24, disp};
    return emit_bytes(w, op, sizeof(op));
}

static int emit_mov_mr8_disp8_rax(struct code_writer *w, unsigned char disp)
{
    const unsigned char op[] = {0x49, 0x89, 0x40, disp};
    return emit_bytes(w, op, sizeof(op));
}

static int emit_mov_mr8_disp8_imm32(struct code_writer *w, unsigned char disp, uint32_t value)
{
    const unsigned char op[] = {0x41, 0xC7, 0x40, disp};
    return emit_bytes(w, op, sizeof(op)) < 0 || emit_u32(w, value) < 0 ? -1 : 0;
}

static int emit_mov_mr8_disp8_imm64_zero(struct code_writer *w, unsigned char disp)
{
    const unsigned char op[] = {0x49, 0xC7, 0x40, disp};
    return emit_bytes(w, op, sizeof(op)) < 0 || emit_u32(w, 0) < 0 ? -1 : 0;
}

static int emit_entry_timestamp_and_tid(struct code_writer *w, size_t *timestamp_done_offset)
{
    static const unsigned char sub_rsp_16[] = {0x48, 0x83, 0xEC, 0x10};
    static const unsigned char mov_eax_gettid[] = {0xB8, 0xBA, 0x00, 0x00, 0x00};
    static const unsigned char syscall_op[] = {0x0F, 0x05};
    static const unsigned char mov_event_tid_eax[] = {
        0x41, 0x89, 0x40, (unsigned char)offsetof(struct probe_event, tid)
    };
    static const unsigned char mov_eax_clock_gettime[] = {0xB8, 0xE4, 0x00, 0x00, 0x00};
    static const unsigned char xor_edi_edi[] = {0x31, 0xFF};
    static const unsigned char mov_rsi_rsp[] = {0x48, 0x89, 0xE6};
    static const unsigned char mov_rax_sec[] = {0x48, 0x8B, 0x04, 0x24};
    static const unsigned char imul_rax_1g[] = {0x48, 0x69, 0xC0, 0x00, 0xCA, 0x9A, 0x3B};
    static const unsigned char add_rax_nsec[] = {0x48, 0x03, 0x44, 0x24, 0x08};
    static const unsigned char mov_event_ts_rax[] = {
        0x49, 0x89, 0x40, (unsigned char)offsetof(struct probe_event, timestamp_ns)
    };
    static const unsigned char add_rsp_16[] = {0x48, 0x83, 0xC4, 0x10};

    if (emit_bytes(w, sub_rsp_16, sizeof(sub_rsp_16)) < 0 ||
        emit_bytes(w, mov_eax_gettid, sizeof(mov_eax_gettid)) < 0 ||
        emit_bytes(w, syscall_op, sizeof(syscall_op)) < 0 ||
        emit_bytes(w, mov_event_tid_eax, sizeof(mov_event_tid_eax)) < 0 ||
        emit_bytes(w, mov_eax_clock_gettime, sizeof(mov_eax_clock_gettime)) < 0 ||
        emit_bytes(w, xor_edi_edi, sizeof(xor_edi_edi)) < 0 ||
        emit_bytes(w, mov_rsi_rsp, sizeof(mov_rsi_rsp)) < 0 ||
        emit_bytes(w, syscall_op, sizeof(syscall_op)) < 0 ||
        emit_bytes(w, mov_rax_sec, sizeof(mov_rax_sec)) < 0 ||
        emit_bytes(w, imul_rax_1g, sizeof(imul_rax_1g)) < 0 ||
        emit_bytes(w, add_rax_nsec, sizeof(add_rax_nsec)) < 0 ||
        emit_bytes(w, mov_event_ts_rax, sizeof(mov_event_ts_rax)) < 0 ||
        emit_bytes(w, add_rsp_16, sizeof(add_rsp_16)) < 0) {
        return -1;
    }

    if (timestamp_done_offset != NULL) {
        *timestamp_done_offset = w->len;
    }
    return 0;
}

static int emit_entry_retprobe_push(struct code_writer *w,
                                    const struct probe_desc *desc,
                                    size_t timestamp_done_offset)
{
    size_t jz_no_ret_imm;
    size_t loop_start;
    size_t try_claim_label;
    size_t jz_try_claim_imm;
    size_t jz_existing_found_imm;
    size_t jz_found_imm;
    size_t jz_claim_race_found_imm;
    size_t jmp_next_existing_imm;
    size_t jmp_next_claim_imm;
    size_t jnz_loop_imm;
    size_t jae_loop_no_ret_imm;
    size_t jae_depth_no_ret_imm;
    size_t jmp_done_imm;
    size_t found_label;
    size_t next_label;
    size_t no_ret_label;
    size_t done_label;
    int32_t rel32;

    if (!desc->has_retprobe || timestamp_done_offset == 0) {
        return 0;
    }

    {
        const unsigned char movabs_r9_shadow[] = {0x49, 0xB9};
        const unsigned char test_r9_r9[] = {0x4D, 0x85, 0xC9};
        const unsigned char jz_rel32[] = {0x0F, 0x84};
        const unsigned char xor_ebx_ebx[] = {0x31, 0xDB};
        const unsigned char cmp_rbx_threads[] = {0x48, 0x81, 0xFB};
        const unsigned char jae_rel32[] = {0x0F, 0x83};
        const unsigned char mov_r11_rbx[] = {0x49, 0x89, 0xDB};
        const unsigned char imul_r11_entry_size[] = {0x4D, 0x69, 0xDB};
        const unsigned char lea_r11_slot[] = {0x4F, 0x8D, 0x1C, 0x19};
        const unsigned char mov_ecx_slot_tid[] = {0x41, 0x8B, 0x0B};
        const unsigned char test_ecx_ecx[] = {0x85, 0xC9};
        const unsigned char jz_rel32_b[] = {0x0F, 0x84};
        const unsigned char jmp_rel32_b[] = {0xE9};
        const unsigned char cmp_ecx_event_tid[] = {
            0x41, 0x3B, 0x48, (unsigned char)offsetof(struct probe_event, tid)
        };
        const unsigned char xor_eax_eax[] = {0x31, 0xC0};
        const unsigned char mov_edx_event_tid[] = {
            0x41, 0x8B, 0x50, (unsigned char)offsetof(struct probe_event, tid)
        };
        const unsigned char lock_cmpxchg_slot_tid[] = {0xF0, 0x41, 0x0F, 0xB1, 0x13};
        const unsigned char cmp_eax_event_tid[] = {
            0x41, 0x3B, 0x40, (unsigned char)offsetof(struct probe_event, tid)
        };

        if (emit_bytes(w, movabs_r9_shadow, sizeof(movabs_r9_shadow)) < 0 ||
            emit_u64(w, desc->shadow_stack_addr) < 0 ||
            emit_bytes(w, test_r9_r9, sizeof(test_r9_r9)) < 0 ||
            emit_bytes(w, jz_rel32, sizeof(jz_rel32)) < 0) {
            return -1;
        }
        jz_no_ret_imm = w->len;
        if (emit_u32(w, 0) < 0 ||
            emit_bytes(w, xor_ebx_ebx, sizeof(xor_ebx_ebx)) < 0) {
            return -1;
        }

        loop_start = w->len;
        if (emit_bytes(w, cmp_rbx_threads, sizeof(cmp_rbx_threads)) < 0 ||
            emit_u32(w, LP_SHADOW_STACK_THREADS) < 0 ||
            emit_bytes(w, jae_rel32, sizeof(jae_rel32)) < 0) {
            return -1;
        }
        jae_loop_no_ret_imm = w->len;
        if (emit_u32(w, 0) < 0 ||
            emit_bytes(w, mov_r11_rbx, sizeof(mov_r11_rbx)) < 0 ||
            emit_bytes(w, imul_r11_entry_size, sizeof(imul_r11_entry_size)) < 0 ||
            emit_u32(w, sizeof(struct lp_shadow_stack_entry)) < 0 ||
            emit_bytes(w, lea_r11_slot, sizeof(lea_r11_slot)) < 0 ||
            emit_bytes(w, mov_ecx_slot_tid, sizeof(mov_ecx_slot_tid)) < 0 ||
            emit_bytes(w, test_ecx_ecx, sizeof(test_ecx_ecx)) < 0 ||
            emit_bytes(w, jz_rel32_b, sizeof(jz_rel32_b)) < 0) {
            return -1;
        }
        jz_try_claim_imm = w->len;
        if (emit_u32(w, 0) < 0 ||
            emit_bytes(w, cmp_ecx_event_tid, sizeof(cmp_ecx_event_tid)) < 0 ||
            emit_bytes(w, jz_rel32_b, sizeof(jz_rel32_b)) < 0) {
            return -1;
        }
        jz_existing_found_imm = w->len;
        if (emit_u32(w, 0) < 0 ||
            emit_bytes(w, jmp_rel32_b, sizeof(jmp_rel32_b)) < 0) {
            return -1;
        }
        jmp_next_existing_imm = w->len;
        if (emit_u32(w, 0) < 0) {
            return -1;
        }

        try_claim_label = w->len;
        if (emit_bytes(w, xor_eax_eax, sizeof(xor_eax_eax)) < 0 ||
            emit_bytes(w, mov_edx_event_tid, sizeof(mov_edx_event_tid)) < 0 ||
            emit_bytes(w, lock_cmpxchg_slot_tid, sizeof(lock_cmpxchg_slot_tid)) < 0 ||
            emit_bytes(w, jz_rel32_b, sizeof(jz_rel32_b)) < 0) {
            return -1;
        }
        jz_found_imm = w->len;
        if (emit_u32(w, 0) < 0 ||
            emit_bytes(w, cmp_eax_event_tid, sizeof(cmp_eax_event_tid)) < 0 ||
            emit_bytes(w, jz_rel32_b, sizeof(jz_rel32_b)) < 0) {
            return -1;
        }
        jz_claim_race_found_imm = w->len;
        if (emit_u32(w, 0) < 0 ||
            emit_bytes(w, jmp_rel32_b, sizeof(jmp_rel32_b)) < 0) {
            return -1;
        }
        jmp_next_claim_imm = w->len;
        if (emit_u32(w, 0) < 0) {
            return -1;
        }
    }

    found_label = w->len;
    {
        const unsigned char mov_eax_event_tid[] = {
            0x41, 0x8B, 0x40, (unsigned char)offsetof(struct probe_event, tid)
        };
        const unsigned char mov_ecx_depth[] = {0x41, 0x8B, 0x4B, 0x04};
        const unsigned char cmp_ecx_depth[] = {0x83, 0xF9, LP_SHADOW_STACK_DEPTH};
        const unsigned char jae_rel32[] = {0x0F, 0x83};
        const unsigned char mov_rdx_retaddr[] = {0x48, 0x8B, 0x94, 0x24, 0x80, 0x00, 0x00, 0x00};
        const unsigned char mov_retaddr_slot[] = {0x49, 0x89, 0x54, 0xCB, 0x08};
        const unsigned char mov_rdx_timestamp[] = {
            0x49, 0x8B, 0x50, (unsigned char)offsetof(struct probe_event, timestamp_ns)
        };
        const unsigned char mov_timestamp_slot[] = {0x49, 0x89, 0x94, 0xCB, 0x08, 0x02, 0x00, 0x00};
        const unsigned char inc_depth[] = {0xFF, 0xC1};
        const unsigned char mov_depth_ecx[] = {0x41, 0x89, 0x4B, 0x04};
        const unsigned char movabs_rdx_retstub[] = {0x48, 0xBA};
        const unsigned char mov_stack_retstub[] = {0x48, 0x89, 0x94, 0x24, 0x80, 0x00, 0x00, 0x00};
        const unsigned char jmp_rel32[] = {0xE9};

        if (emit_bytes(w, mov_eax_event_tid, sizeof(mov_eax_event_tid)) < 0 ||
            emit_bytes(w, mov_ecx_depth, sizeof(mov_ecx_depth)) < 0 ||
            emit_bytes(w, cmp_ecx_depth, sizeof(cmp_ecx_depth)) < 0 ||
            emit_bytes(w, jae_rel32, sizeof(jae_rel32)) < 0) {
            return -1;
        }
        jae_depth_no_ret_imm = w->len;
        if (emit_u32(w, 0) < 0 ||
            emit_bytes(w, mov_rdx_retaddr, sizeof(mov_rdx_retaddr)) < 0 ||
            emit_bytes(w, mov_retaddr_slot, sizeof(mov_retaddr_slot)) < 0 ||
            emit_bytes(w, mov_rdx_timestamp, sizeof(mov_rdx_timestamp)) < 0 ||
            emit_bytes(w, mov_timestamp_slot, sizeof(mov_timestamp_slot)) < 0 ||
            emit_bytes(w, inc_depth, sizeof(inc_depth)) < 0 ||
            emit_bytes(w, mov_depth_ecx, sizeof(mov_depth_ecx)) < 0 ||
            emit_bytes(w, movabs_rdx_retstub, sizeof(movabs_rdx_retstub)) < 0 ||
            emit_u64(w, desc->ret_stub_addr) < 0 ||
            emit_bytes(w, mov_stack_retstub, sizeof(mov_stack_retstub)) < 0 ||
            emit_bytes(w, jmp_rel32, sizeof(jmp_rel32)) < 0) {
            return -1;
        }
        jmp_done_imm = w->len;
        if (emit_u32(w, 0) < 0) {
            return -1;
        }
    }

    next_label = w->len;
    {
        const unsigned char inc_rbx[] = {0x48, 0xFF, 0xC3};
        const unsigned char jmp_rel32[] = {0xE9};
        if (emit_bytes(w, inc_rbx, sizeof(inc_rbx)) < 0 ||
            emit_bytes(w, jmp_rel32, sizeof(jmp_rel32)) < 0) {
            return -1;
        }
        jnz_loop_imm = w->len;
        if (emit_u32(w, 0) < 0) {
            return -1;
        }
    }

    no_ret_label = w->len;
    done_label = w->len;

    rel32 = (int32_t)(no_ret_label - (jz_no_ret_imm + sizeof(uint32_t)));
    memcpy(w->buf + jz_no_ret_imm, &rel32, sizeof(rel32));
    rel32 = (int32_t)(try_claim_label - (jz_try_claim_imm + sizeof(uint32_t)));
    memcpy(w->buf + jz_try_claim_imm, &rel32, sizeof(rel32));
    rel32 = (int32_t)(found_label - (jz_existing_found_imm + sizeof(uint32_t)));
    memcpy(w->buf + jz_existing_found_imm, &rel32, sizeof(rel32));
    rel32 = (int32_t)(found_label - (jz_found_imm + sizeof(uint32_t)));
    memcpy(w->buf + jz_found_imm, &rel32, sizeof(rel32));
    rel32 = (int32_t)(found_label - (jz_claim_race_found_imm + sizeof(uint32_t)));
    memcpy(w->buf + jz_claim_race_found_imm, &rel32, sizeof(rel32));
    rel32 = (int32_t)(next_label - (jmp_next_existing_imm + sizeof(uint32_t)));
    memcpy(w->buf + jmp_next_existing_imm, &rel32, sizeof(rel32));
    rel32 = (int32_t)(next_label - (jmp_next_claim_imm + sizeof(uint32_t)));
    memcpy(w->buf + jmp_next_claim_imm, &rel32, sizeof(rel32));
    rel32 = (int32_t)(no_ret_label - (jae_loop_no_ret_imm + sizeof(uint32_t)));
    memcpy(w->buf + jae_loop_no_ret_imm, &rel32, sizeof(rel32));
    rel32 = (int32_t)(no_ret_label - (jae_depth_no_ret_imm + sizeof(uint32_t)));
    memcpy(w->buf + jae_depth_no_ret_imm, &rel32, sizeof(rel32));
    rel32 = (int32_t)(loop_start - (jnz_loop_imm + sizeof(uint32_t)));
    memcpy(w->buf + jnz_loop_imm, &rel32, sizeof(rel32));
    rel32 = (int32_t)(done_label - (jmp_done_imm + sizeof(uint32_t)));
    memcpy(w->buf + jmp_done_imm, &rel32, sizeof(rel32));

    (void)timestamp_done_offset;
    return 0;
}

int lp_x86_64_build_trampoline(const struct probe_desc *desc,
                               uint64_t resume_addr,
                               unsigned char *buf,
                               size_t buf_len,
                               size_t *written)
{
    struct code_writer w = {
        .buf = buf,
        .cap = buf_len,
    };

    if (desc->original_len <= 0 || desc->original_len > LP_MAX_ORIGINAL_CODE) {
        errno = EINVAL;
        return -1;
    }
    if (emit_bytes(&w, desc->original_code, (size_t)desc->original_len) < 0 ||
        emit_abs_jmp_r11_preserve(&w, resume_addr) < 0) {
        return -1;
    }

    if (written != NULL) {
        *written = w.len;
    }
    return 0;
}

int lp_x86_64_build_entry_stub(const struct probe_desc *desc,
                               unsigned char *buf,
                               size_t buf_len,
                               size_t *written)
{
    struct code_writer w = {
        .buf = buf,
        .cap = buf_len,
    };
    size_t je_imm_offset;
    size_t timestamp_done_offset = 0;
    uint32_t je_rel32;

    static const unsigned char save_regs[] = {
        0x9C,                         /* pushfq */
        0x50, 0x53, 0x51, 0x52,       /* push rax/rbx/rcx/rdx */
        0x56, 0x57, 0x55,             /* push rsi/rdi/rbp */
        0x41, 0x50, 0x41, 0x51,       /* push r8/r9 */
        0x41, 0x52, 0x41, 0x53,       /* push r10/r11 */
        0x41, 0x54, 0x41, 0x55,       /* push r12/r13 */
        0x41, 0x56, 0x41, 0x57,       /* push r14/r15 */
    };
    static const unsigned char restore_regs[] = {
        0x41, 0x5F, 0x41, 0x5E,       /* pop r15/r14 */
        0x41, 0x5D, 0x41, 0x5C,       /* pop r13/r12 */
        0x41, 0x5B, 0x41, 0x5A,       /* pop r11/r10 */
        0x41, 0x59, 0x41, 0x58,       /* pop r9/r8 */
        0x5D, 0x5F, 0x5E,             /* pop rbp/rdi/rsi */
        0x5A, 0x59, 0x5B, 0x58,       /* pop rdx/rcx/rbx/rax */
        0x9D,                         /* popfq */
    };

    if (emit_bytes(&w, save_regs, sizeof(save_regs)) < 0 ||
        emit_movabs_r10(&w, desc->config_addr) < 0) {
        return -1;
    }

    {
        const unsigned char cmp_enabled[] = {0x41, 0x83, 0x3A, 0x00};
        const unsigned char je_rel32_op[] = {0x0F, 0x84};
        const unsigned char load_event_buf[] = {0x4D, 0x8B, 0x5A, 0x08};
        const unsigned char mov_eax_1[] = {0xB8, 0x01, 0x00, 0x00, 0x00};
        const unsigned char lock_xadd[] = {0xF0, 0x49, 0x0F, 0xC1, 0x03};
        const unsigned char and_eax_mask[] = {0x25, 0xFF, 0x0F, 0x00, 0x00};
        const unsigned char mov_r8_rax[] = {0x49, 0x89, 0xC0};
        const unsigned char shl_r8_6[] = {0x49, 0xC1, 0xE0, 0x06};
        const unsigned char mov_r9_rax[] = {0x49, 0x89, 0xC1};
        const unsigned char shl_r9_4[] = {0x49, 0xC1, 0xE1, 0x04};
        const unsigned char shl_r9_3[] = {0x49, 0xC1, 0xE1, 0x03};
        const unsigned char add_r8_r9[] = {0x4D, 0x01, 0xC8};
        const unsigned char lea_event[] = {0x4F, 0x8D, 0x44, 0x03, 0x10};

        if (emit_bytes(&w, cmp_enabled, sizeof(cmp_enabled)) < 0 ||
            emit_bytes(&w, je_rel32_op, sizeof(je_rel32_op)) < 0) {
            return -1;
        }
        je_imm_offset = w.len;
        if (emit_u32(&w, 0) < 0 ||
            emit_bytes(&w, load_event_buf, sizeof(load_event_buf)) < 0 ||
            emit_bytes(&w, mov_eax_1, sizeof(mov_eax_1)) < 0 ||
            emit_bytes(&w, lock_xadd, sizeof(lock_xadd)) < 0 ||
            emit_bytes(&w, and_eax_mask, sizeof(and_eax_mask)) < 0 ||
            emit_bytes(&w, mov_r8_rax, sizeof(mov_r8_rax)) < 0 ||
            emit_bytes(&w, shl_r8_6, sizeof(shl_r8_6)) < 0 ||
            emit_bytes(&w, mov_r9_rax, sizeof(mov_r9_rax)) < 0 ||
            emit_bytes(&w, shl_r9_4, sizeof(shl_r9_4)) < 0 ||
            emit_bytes(&w, add_r8_r9, sizeof(add_r8_r9)) < 0 ||
            emit_bytes(&w, mov_r9_rax, sizeof(mov_r9_rax)) < 0 ||
            emit_bytes(&w, shl_r9_3, sizeof(shl_r9_3)) < 0 ||
            emit_bytes(&w, add_r8_r9, sizeof(add_r8_r9)) < 0 ||
            emit_bytes(&w, lea_event, sizeof(lea_event)) < 0) {
            return -1;
        }
    }

    if (emit_mov_mr8_disp8_imm32(&w, (unsigned char)offsetof(struct probe_event, pid),
                                 (uint32_t)desc->pid) < 0 ||
        emit_mov_mr8_disp8_imm32(&w, (unsigned char)offsetof(struct probe_event, probe_id),
                                 (uint32_t)desc->probe_id) < 0 ||
        emit_mov_mr8_disp8_imm32(&w, (unsigned char)offsetof(struct probe_event, event_type),
                                 PROBE_EVENT_ENTRY) < 0) {
        return -1;
    }

    static const unsigned char saved_arg_offsets[] = {72, 80, 88, 96, 56, 48};
    for (size_t i = 0; i < 6; i++) {
        unsigned char event_arg_offset =
            (unsigned char)(offsetof(struct probe_event, args) + i * sizeof(uint64_t));
        if (emit_mov_rax_from_rsp_disp8(&w, saved_arg_offsets[i]) < 0 ||
            emit_mov_mr8_disp8_rax(&w, event_arg_offset) < 0) {
            return -1;
        }
    }

    if (emit_entry_timestamp_and_tid(&w, &timestamp_done_offset) < 0 ||
        emit_mov_mr8_disp8_imm64_zero(&w, (unsigned char)offsetof(struct probe_event, retval)) < 0 ||
        emit_mov_mr8_disp8_imm64_zero(&w, (unsigned char)offsetof(struct probe_event, duration_ns)) < 0) {
        return -1;
    }

    if (emit_entry_retprobe_push(&w, desc, timestamp_done_offset) < 0) {
        return -1;
    }

    je_rel32 = (uint32_t)(w.len - (je_imm_offset + sizeof(uint32_t)));
    memcpy(w.buf + je_imm_offset, &je_rel32, sizeof(je_rel32));

    if (emit_bytes(&w, restore_regs, sizeof(restore_regs)) < 0 ||
        emit_abs_jmp_r11_preserve(&w, desc->trampoline_addr) < 0) {
        return -1;
    }

    if (written != NULL) {
        *written = w.len;
    }
    return 0;
}

int lp_x86_64_build_ret_stub(const struct probe_desc *desc,
                             unsigned char *buf,
                             size_t buf_len,
                             size_t *written)
{
    struct code_writer w = {
        .buf = buf,
        .cap = buf_len,
    };
    size_t loop_start;
    size_t jz_found_imm;
    size_t jmp_next_imm;
    size_t jae_loop_fallback_imm;
    size_t jz_empty_fallback_imm;
    size_t jmp_loop_imm;
    size_t found_label;
    size_t next_label;
    size_t fallback_label;
    int32_t rel32;

    static const unsigned char save[] = {
        0x9C,                         /* pushfq */
        0x50,                         /* push rax, preserve retval */
        0x53, 0x51, 0x52,             /* push rbx/rcx/rdx */
        0x56, 0x57,                   /* push rsi/rdi */
        0x41, 0x50, 0x41, 0x51,       /* push r8/r9 */
        0x41, 0x52, 0x41, 0x53,       /* push r10/r11 */
        0x41, 0x54,                   /* push r12 */
    };
    static const unsigned char restore_no_rax[] = {
        0x41, 0x5C,                   /* pop r12 */
        0x41, 0x5B, 0x41, 0x5A,       /* pop r11/r10 */
        0x41, 0x59, 0x41, 0x58,       /* pop r9/r8 */
        0x5F, 0x5E,                   /* pop rdi/rsi */
        0x5A, 0x59, 0x5B,             /* pop rdx/rcx/rbx */
        0x58,                         /* pop rax */
        0x9D,                         /* popfq */
    };

    if (desc->shadow_stack_addr == 0) {
        errno = EINVAL;
        return -1;
    }

    if (emit_bytes(&w, save, sizeof(save)) < 0 ||
        emit_movabs_r10(&w, desc->shadow_stack_addr) < 0) {
        return -1;
    }

    {
        const unsigned char mov_eax_gettid[] = {0xB8, 0xBA, 0x00, 0x00, 0x00};
        const unsigned char syscall_op[] = {0x0F, 0x05};
        const unsigned char mov_r8d_eax[] = {0x41, 0x89, 0xC0};
        const unsigned char xor_ebx_ebx[] = {0x31, 0xDB};
        const unsigned char cmp_rbx_threads[] = {0x48, 0x81, 0xFB};
        const unsigned char jae_rel32[] = {0x0F, 0x83};
        const unsigned char mov_r11_rbx[] = {0x49, 0x89, 0xDB};
        const unsigned char imul_r11_entry_size[] = {0x4D, 0x69, 0xDB};
        const unsigned char lea_r11_slot[] = {0x4F, 0x8D, 0x1C, 0x1A};
        const unsigned char mov_ecx_slot_tid[] = {0x41, 0x8B, 0x0B};
        const unsigned char cmp_ecx_r8d[] = {0x44, 0x39, 0xC1};
        const unsigned char jz_rel32[] = {0x0F, 0x84};
        const unsigned char jmp_rel32_b[] = {0xE9};

        if (emit_bytes(&w, mov_eax_gettid, sizeof(mov_eax_gettid)) < 0 ||
            emit_bytes(&w, syscall_op, sizeof(syscall_op)) < 0 ||
            emit_bytes(&w, mov_r8d_eax, sizeof(mov_r8d_eax)) < 0 ||
            emit_bytes(&w, xor_ebx_ebx, sizeof(xor_ebx_ebx)) < 0) {
            return -1;
        }

        loop_start = w.len;
        if (emit_bytes(&w, cmp_rbx_threads, sizeof(cmp_rbx_threads)) < 0 ||
            emit_u32(&w, LP_SHADOW_STACK_THREADS) < 0 ||
            emit_bytes(&w, jae_rel32, sizeof(jae_rel32)) < 0) {
            return -1;
        }
        jae_loop_fallback_imm = w.len;
        if (emit_u32(&w, 0) < 0 ||
            emit_bytes(&w, mov_r11_rbx, sizeof(mov_r11_rbx)) < 0 ||
            emit_bytes(&w, imul_r11_entry_size, sizeof(imul_r11_entry_size)) < 0 ||
            emit_u32(&w, sizeof(struct lp_shadow_stack_entry)) < 0 ||
            emit_bytes(&w, lea_r11_slot, sizeof(lea_r11_slot)) < 0 ||
            emit_bytes(&w, mov_ecx_slot_tid, sizeof(mov_ecx_slot_tid)) < 0 ||
            emit_bytes(&w, cmp_ecx_r8d, sizeof(cmp_ecx_r8d)) < 0 ||
            emit_bytes(&w, jz_rel32, sizeof(jz_rel32)) < 0) {
            return -1;
        }
        jz_found_imm = w.len;
        if (emit_u32(&w, 0) < 0 ||
            emit_bytes(&w, jmp_rel32_b, sizeof(jmp_rel32_b)) < 0) {
            return -1;
        }
        jmp_next_imm = w.len;
        if (emit_u32(&w, 0) < 0) {
            return -1;
        }
    }

    found_label = w.len;
    {
        const unsigned char mov_ecx_depth[] = {0x41, 0x8B, 0x4B, 0x04};
        const unsigned char test_ecx_ecx[] = {0x85, 0xC9};
        const unsigned char jz_rel32[] = {0x0F, 0x84};
        const unsigned char dec_ecx[] = {0xFF, 0xC9};
        const unsigned char mov_depth_ecx[] = {0x41, 0x89, 0x4B, 0x04};
        const unsigned char mov_rdx_retaddr[] = {0x49, 0x8B, 0x54, 0xCB, 0x08};
        const unsigned char mov_saved_r11[] = {0x48, 0x89, 0x54, 0x24, 0x08};
        const unsigned char mov_rbx_r11[] = {0x4C, 0x89, 0xDB};
        const unsigned char mov_r12_rcx[] = {0x49, 0x89, 0xCC};
        const unsigned char movabs_r10_config[] = {0x49, 0xBA};
        const unsigned char load_event_buf[] = {0x4D, 0x8B, 0x52, 0x08};
        const unsigned char mov_eax_1[] = {0xB8, 0x01, 0x00, 0x00, 0x00};
        const unsigned char lock_xadd[] = {0xF0, 0x49, 0x0F, 0xC1, 0x02};
        const unsigned char and_eax_mask[] = {0x25, 0xFF, 0x0F, 0x00, 0x00};
        const unsigned char mov_r8_rax[] = {0x49, 0x89, 0xC0};
        const unsigned char shl_r8_6[] = {0x49, 0xC1, 0xE0, 0x06};
        const unsigned char mov_r9_rax[] = {0x49, 0x89, 0xC1};
        const unsigned char shl_r9_4[] = {0x49, 0xC1, 0xE1, 0x04};
        const unsigned char shl_r9_3[] = {0x49, 0xC1, 0xE1, 0x03};
        const unsigned char add_r8_r9[] = {0x4D, 0x01, 0xC8};
        const unsigned char lea_event[] = {0x4F, 0x8D, 0x44, 0x02, 0x10};
        const unsigned char mov_saved_rax_to_rdx[] = {0x48, 0x8B, 0x54, 0x24, 0x50};
        const unsigned char mov_event_retval_rdx[] = {
            0x49, 0x89, 0x50, (unsigned char)offsetof(struct probe_event, retval)
        };
        const unsigned char sub_rsp_16[] = {0x48, 0x83, 0xEC, 0x10};
        const unsigned char mov_eax_clock_gettime[] = {0xB8, 0xE4, 0x00, 0x00, 0x00};
        const unsigned char xor_edi_edi[] = {0x31, 0xFF};
        const unsigned char mov_rsi_rsp[] = {0x48, 0x89, 0xE6};
        const unsigned char syscall_op[] = {0x0F, 0x05};
        const unsigned char mov_rax_sec[] = {0x48, 0x8B, 0x04, 0x24};
        const unsigned char imul_rax_1g[] = {0x48, 0x69, 0xC0, 0x00, 0xCA, 0x9A, 0x3B};
        const unsigned char add_rax_nsec[] = {0x48, 0x03, 0x44, 0x24, 0x08};
        const unsigned char add_rsp_16[] = {0x48, 0x83, 0xC4, 0x10};
        const unsigned char mov_event_ts_rax[] = {
            0x49, 0x89, 0x40, (unsigned char)offsetof(struct probe_event, timestamp_ns)
        };
        const unsigned char mov_edx_slot_tid[] = {0x8B, 0x13};
        const unsigned char mov_event_tid_edx[] = {
            0x41, 0x89, 0x50, (unsigned char)offsetof(struct probe_event, tid)
        };
        const unsigned char mov_rdx_entry_ts[] = {0x4A, 0x8B, 0x94, 0xE3, 0x08, 0x02, 0x00, 0x00};
        const unsigned char sub_rax_rdx[] = {0x48, 0x29, 0xD0};
        const unsigned char mov_event_duration_rax[] = {
            0x49, 0x89, 0x40, (unsigned char)offsetof(struct probe_event, duration_ns)
        };
        const unsigned char jmp_r11[] = {0x41, 0xFF, 0xE3};

        if (emit_bytes(&w, mov_ecx_depth, sizeof(mov_ecx_depth)) < 0 ||
            emit_bytes(&w, test_ecx_ecx, sizeof(test_ecx_ecx)) < 0 ||
            emit_bytes(&w, jz_rel32, sizeof(jz_rel32)) < 0) {
            return -1;
        }
        jz_empty_fallback_imm = w.len;
        if (emit_u32(&w, 0) < 0 ||
            emit_bytes(&w, dec_ecx, sizeof(dec_ecx)) < 0 ||
            emit_bytes(&w, mov_depth_ecx, sizeof(mov_depth_ecx)) < 0 ||
            emit_bytes(&w, mov_rdx_retaddr, sizeof(mov_rdx_retaddr)) < 0 ||
            emit_bytes(&w, mov_saved_r11, sizeof(mov_saved_r11)) < 0 ||
            emit_bytes(&w, mov_rbx_r11, sizeof(mov_rbx_r11)) < 0 ||
            emit_bytes(&w, mov_r12_rcx, sizeof(mov_r12_rcx)) < 0 ||
            emit_bytes(&w, movabs_r10_config, sizeof(movabs_r10_config)) < 0 ||
            emit_u64(&w, desc->config_addr) < 0 ||
            emit_bytes(&w, load_event_buf, sizeof(load_event_buf)) < 0 ||
            emit_bytes(&w, mov_eax_1, sizeof(mov_eax_1)) < 0 ||
            emit_bytes(&w, lock_xadd, sizeof(lock_xadd)) < 0 ||
            emit_bytes(&w, and_eax_mask, sizeof(and_eax_mask)) < 0 ||
            emit_bytes(&w, mov_r8_rax, sizeof(mov_r8_rax)) < 0 ||
            emit_bytes(&w, shl_r8_6, sizeof(shl_r8_6)) < 0 ||
            emit_bytes(&w, mov_r9_rax, sizeof(mov_r9_rax)) < 0 ||
            emit_bytes(&w, shl_r9_4, sizeof(shl_r9_4)) < 0 ||
            emit_bytes(&w, add_r8_r9, sizeof(add_r8_r9)) < 0 ||
            emit_bytes(&w, mov_r9_rax, sizeof(mov_r9_rax)) < 0 ||
            emit_bytes(&w, shl_r9_3, sizeof(shl_r9_3)) < 0 ||
            emit_bytes(&w, add_r8_r9, sizeof(add_r8_r9)) < 0 ||
            emit_bytes(&w, lea_event, sizeof(lea_event)) < 0 ||
            emit_mov_mr8_disp8_imm32(&w, (unsigned char)offsetof(struct probe_event, pid),
                                     (uint32_t)desc->pid) < 0 ||
            emit_bytes(&w, mov_edx_slot_tid, sizeof(mov_edx_slot_tid)) < 0 ||
            emit_bytes(&w, mov_event_tid_edx, sizeof(mov_event_tid_edx)) < 0 ||
            emit_mov_mr8_disp8_imm32(&w, (unsigned char)offsetof(struct probe_event, probe_id),
                                     (uint32_t)desc->probe_id) < 0 ||
            emit_mov_mr8_disp8_imm32(&w, (unsigned char)offsetof(struct probe_event, event_type),
                                     PROBE_EVENT_RETURN) < 0 ||
            emit_mov_mr8_disp8_imm64_zero(&w, (unsigned char)offsetof(struct probe_event, args)) < 0 ||
            emit_mov_mr8_disp8_imm64_zero(&w, (unsigned char)(offsetof(struct probe_event, args) + 8)) < 0 ||
            emit_mov_mr8_disp8_imm64_zero(&w, (unsigned char)(offsetof(struct probe_event, args) + 16)) < 0 ||
            emit_mov_mr8_disp8_imm64_zero(&w, (unsigned char)(offsetof(struct probe_event, args) + 24)) < 0 ||
            emit_mov_mr8_disp8_imm64_zero(&w, (unsigned char)(offsetof(struct probe_event, args) + 32)) < 0 ||
            emit_mov_mr8_disp8_imm64_zero(&w, (unsigned char)(offsetof(struct probe_event, args) + 40)) < 0 ||
            emit_bytes(&w, mov_saved_rax_to_rdx, sizeof(mov_saved_rax_to_rdx)) < 0 ||
            emit_bytes(&w, mov_event_retval_rdx, sizeof(mov_event_retval_rdx)) < 0 ||
            emit_bytes(&w, sub_rsp_16, sizeof(sub_rsp_16)) < 0 ||
            emit_bytes(&w, mov_eax_clock_gettime, sizeof(mov_eax_clock_gettime)) < 0 ||
            emit_bytes(&w, xor_edi_edi, sizeof(xor_edi_edi)) < 0 ||
            emit_bytes(&w, mov_rsi_rsp, sizeof(mov_rsi_rsp)) < 0 ||
            emit_bytes(&w, syscall_op, sizeof(syscall_op)) < 0 ||
            emit_bytes(&w, mov_rax_sec, sizeof(mov_rax_sec)) < 0 ||
            emit_bytes(&w, imul_rax_1g, sizeof(imul_rax_1g)) < 0 ||
            emit_bytes(&w, add_rax_nsec, sizeof(add_rax_nsec)) < 0 ||
            emit_bytes(&w, add_rsp_16, sizeof(add_rsp_16)) < 0 ||
            emit_bytes(&w, mov_event_ts_rax, sizeof(mov_event_ts_rax)) < 0 ||
            emit_bytes(&w, mov_rdx_entry_ts, sizeof(mov_rdx_entry_ts)) < 0 ||
            emit_bytes(&w, sub_rax_rdx, sizeof(sub_rax_rdx)) < 0 ||
            emit_bytes(&w, mov_event_duration_rax, sizeof(mov_event_duration_rax)) < 0 ||
            emit_bytes(&w, restore_no_rax, sizeof(restore_no_rax)) < 0 ||
            emit_bytes(&w, jmp_r11, sizeof(jmp_r11)) < 0) {
            return -1;
        }
    }

    next_label = w.len;
    {
        const unsigned char inc_rbx[] = {0x48, 0xFF, 0xC3};
        const unsigned char jmp_rel32[] = {0xE9};
        if (emit_bytes(&w, inc_rbx, sizeof(inc_rbx)) < 0 ||
            emit_bytes(&w, jmp_rel32, sizeof(jmp_rel32)) < 0) {
            return -1;
        }
        jmp_loop_imm = w.len;
        if (emit_u32(&w, 0) < 0) {
            return -1;
        }
    }

    fallback_label = w.len;
    {
        const unsigned char ud2[] = {0x0F, 0x0B};
        if (emit_bytes(&w, ud2, sizeof(ud2)) < 0) {
            return -1;
        }
    }

    rel32 = (int32_t)(found_label - (jz_found_imm + sizeof(uint32_t)));
    memcpy(w.buf + jz_found_imm, &rel32, sizeof(rel32));
    rel32 = (int32_t)(next_label - (jmp_next_imm + sizeof(uint32_t)));
    memcpy(w.buf + jmp_next_imm, &rel32, sizeof(rel32));
    rel32 = (int32_t)(loop_start - (jmp_loop_imm + sizeof(uint32_t)));
    memcpy(w.buf + jmp_loop_imm, &rel32, sizeof(rel32));
    rel32 = (int32_t)(fallback_label - (jae_loop_fallback_imm + sizeof(uint32_t)));
    memcpy(w.buf + jae_loop_fallback_imm, &rel32, sizeof(rel32));
    rel32 = (int32_t)(fallback_label - (jz_empty_fallback_imm + sizeof(uint32_t)));
    memcpy(w.buf + jz_empty_fallback_imm, &rel32, sizeof(rel32));

    if (written != NULL) {
        *written = w.len;
    }
    return 0;
}
