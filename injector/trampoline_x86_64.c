#include "injector.h"

#include "arch_x86_64.h"
#include "event.h"

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

    if (emit_mov_mr8_disp8_imm64_zero(&w, (unsigned char)offsetof(struct probe_event, timestamp_ns)) < 0 ||
        emit_mov_mr8_disp8_imm32(&w, (unsigned char)offsetof(struct probe_event, pid),
                                 (uint32_t)desc->pid) < 0 ||
        emit_mov_mr8_disp8_imm32(&w, (unsigned char)offsetof(struct probe_event, tid), 0) < 0 ||
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

    if (emit_mov_mr8_disp8_imm64_zero(&w, (unsigned char)offsetof(struct probe_event, retval)) < 0 ||
        emit_mov_mr8_disp8_imm64_zero(&w, (unsigned char)offsetof(struct probe_event, duration_ns)) < 0) {
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
    (void)desc;

    if (buf_len < 1) {
        errno = ENOSPC;
        return -1;
    }

    buf[0] = 0xC3;
    if (written != NULL) {
        *written = 1;
    }
    return 0;
}
