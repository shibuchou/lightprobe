#include "injector.h"

#include <errno.h>
#include <stdint.h>

static int has_modrm_one_byte(unsigned char opcode)
{
    if ((opcode <= 0x03) ||
        (opcode >= 0x08 && opcode <= 0x0B) ||
        (opcode >= 0x10 && opcode <= 0x13) ||
        (opcode >= 0x18 && opcode <= 0x1B) ||
        (opcode >= 0x20 && opcode <= 0x23) ||
        (opcode >= 0x28 && opcode <= 0x2B) ||
        (opcode >= 0x30 && opcode <= 0x33) ||
        (opcode >= 0x38 && opcode <= 0x3B) ||
        (opcode >= 0x62 && opcode <= 0x63) ||
        opcode == 0x69 || opcode == 0x6B ||
        (opcode >= 0x80 && opcode <= 0x8F) ||
        (opcode >= 0xC0 && opcode <= 0xC1) ||
        (opcode >= 0xC4 && opcode <= 0xC7) ||
        (opcode >= 0xD0 && opcode <= 0xD3) ||
        (opcode >= 0xF6 && opcode <= 0xF7) ||
        opcode >= 0xFE) {
        return 1;
    }
    return 0;
}

static int has_modrm_two_byte(unsigned char opcode)
{
    if ((opcode >= 0x10 && opcode <= 0x1F) ||
        (opcode >= 0x28 && opcode <= 0x2F) ||
        (opcode >= 0x40 && opcode <= 0x4F) ||
        (opcode >= 0x90 && opcode <= 0x9F) ||
        (opcode >= 0xA3 && opcode <= 0xA5) ||
        (opcode >= 0xAB && opcode <= 0xAF) ||
        (opcode >= 0xB0 && opcode <= 0xB7) ||
        (opcode >= 0xBA && opcode <= 0xBF)) {
        return 1;
    }
    return 0;
}

static size_t imm_len_one_byte(unsigned char opcode, int rex_w, unsigned char modrm_reg)
{
    (void)rex_w;

    if ((opcode >= 0x70 && opcode <= 0x7F) || opcode == 0xEB) {
        return 1;
    }
    if (opcode == 0xE8 || opcode == 0xE9 ||
        (opcode >= 0xA0 && opcode <= 0xA3)) {
        return 4;
    }
    if (opcode == 0xC2 || opcode == 0xCA) {
        return 2;
    }
    if (opcode == 0xC8) {
        return 3;
    }
    if (opcode == 0xCD || opcode == 0x6A) {
        return 1;
    }
    if (opcode == 0x68) {
        return 4;
    }
    if ((opcode >= 0xB8 && opcode <= 0xBF)) {
        return rex_w ? 8 : 4;
    }
    if (opcode == 0xC6) {
        return 1;
    }
    if (opcode == 0xC7) {
        return 4;
    }
    if (opcode == 0x80 || opcode == 0x82 || opcode == 0x83 ||
        opcode == 0xC0 || opcode == 0xC1 || opcode == 0x6B) {
        return 1;
    }
    if (opcode == 0x81 || opcode == 0x69) {
        return 4;
    }
    if ((opcode == 0xF6 && modrm_reg == 0) ||
        (opcode == 0xF7 && modrm_reg == 0)) {
        return opcode == 0xF6 ? 1 : 4;
    }
    return 0;
}

static size_t imm_len_two_byte(unsigned char opcode)
{
    if ((opcode >= 0x80 && opcode <= 0x8F)) {
        return 4;
    }
    if (opcode == 0xBA) {
        return 1;
    }
    return 0;
}

static int read_modrm_len(const unsigned char *code,
                          size_t max_len,
                          size_t *offset,
                          unsigned char *modrm_out)
{
    if (*offset >= max_len) {
        errno = EINVAL;
        return -1;
    }

    unsigned char modrm = code[(*offset)++];
    unsigned char mod = (unsigned char)(modrm >> 6);
    unsigned char rm = (unsigned char)(modrm & 0x07);

    if (modrm_out != NULL) {
        *modrm_out = modrm;
    }

    if (mod != 3 && rm == 4) {
        if (*offset >= max_len) {
            errno = EINVAL;
            return -1;
        }
        unsigned char sib = code[(*offset)++];
        unsigned char base = (unsigned char)(sib & 0x07);
        if (mod == 0 && base == 5) {
            *offset += 4;
        }
    }

    if (mod == 0 && rm == 5) {
        *offset += 4;
    } else if (mod == 1) {
        *offset += 1;
    } else if (mod == 2) {
        *offset += 4;
    }

    if (*offset > max_len) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int lp_x86_64_insn_len(const unsigned char *code, size_t max_len, size_t *insn_len)
{
    size_t off = 0;
    int rex_w = 0;
    int two_byte = 0;
    unsigned char opcode;
    unsigned char modrm = 0;
    int has_modrm;
    size_t imm_len;

    if (code == NULL || insn_len == NULL || max_len == 0) {
        errno = EINVAL;
        return -1;
    }

    while (off < max_len) {
        unsigned char b = code[off];
        if (b == 0x66 || b == 0x67 || b == 0xF0 || b == 0xF2 || b == 0xF3 ||
            b == 0x2E || b == 0x36 || b == 0x3E || b == 0x26 || b == 0x64 || b == 0x65) {
            off++;
            continue;
        }
        if (b >= 0x40 && b <= 0x4F) {
            rex_w = (b & 0x08) != 0;
            off++;
            continue;
        }
        break;
    }

    if (off >= max_len) {
        errno = EINVAL;
        return -1;
    }

    opcode = code[off++];
    if (opcode == 0x0F) {
        if (off >= max_len) {
            errno = EINVAL;
            return -1;
        }
        two_byte = 1;
        opcode = code[off++];
    }

    has_modrm = two_byte ? has_modrm_two_byte(opcode) : has_modrm_one_byte(opcode);
    if (has_modrm) {
        if (read_modrm_len(code, max_len, &off, &modrm) < 0) {
            return -1;
        }
    }

    imm_len = two_byte ? imm_len_two_byte(opcode)
                       : imm_len_one_byte(opcode, rex_w, (unsigned char)((modrm >> 3) & 0x07));
    off += imm_len;

    if (off == 0 || off > max_len || off > 15) {
        errno = EINVAL;
        return -1;
    }

    *insn_len = off;
    return 0;
}

int lp_x86_64_calc_patch_len(const unsigned char *code,
                             size_t code_len,
                             size_t min_len,
                             size_t *patch_len)
{
    size_t total = 0;

    if (code == NULL || patch_len == NULL || min_len == 0) {
        errno = EINVAL;
        return -1;
    }

    while (total < min_len) {
        size_t one_len = 0;
        if (total >= code_len) {
            errno = EINVAL;
            return -1;
        }
        if (lp_x86_64_insn_len(code + total, code_len - total, &one_len) < 0) {
            return -1;
        }
        total += one_len;
        if (total > LP_MAX_ORIGINAL_CODE) {
            errno = EOVERFLOW;
            return -1;
        }
    }

    *patch_len = total;
    return 0;
}
