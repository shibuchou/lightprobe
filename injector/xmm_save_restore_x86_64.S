.text
.align 16
.globl lp_x86_64_save_xmm
.type lp_x86_64_save_xmm, @function
lp_x86_64_save_xmm:
    sub $0x100, %rsp
    movdqu %xmm0, 0x00(%rsp)
    movdqu %xmm1, 0x10(%rsp)
    movdqu %xmm2, 0x20(%rsp)
    movdqu %xmm3, 0x30(%rsp)
    movdqu %xmm4, 0x40(%rsp)
    movdqu %xmm5, 0x50(%rsp)
    movdqu %xmm6, 0x60(%rsp)
    movdqu %xmm7, 0x70(%rsp)
    movdqu %xmm8, 0x80(%rsp)
    movdqu %xmm9, 0x90(%rsp)
    movdqu %xmm10, 0xA0(%rsp)
    movdqu %xmm11, 0xB0(%rsp)
    movdqu %xmm12, 0xC0(%rsp)
    movdqu %xmm13, 0xD0(%rsp)
    movdqu %xmm14, 0xE0(%rsp)
    movdqu %xmm15, 0xF0(%rsp)
lp_x86_64_save_xmm_end:
    ret

.align 16
.globl lp_x86_64_restore_xmm
.type lp_x86_64_restore_xmm, @function
lp_x86_64_restore_xmm:
    movdqu 0x00(%rsp), %xmm0
    movdqu 0x10(%rsp), %xmm1
    movdqu 0x20(%rsp), %xmm2
    movdqu 0x30(%rsp), %xmm3
    movdqu 0x40(%rsp), %xmm4
    movdqu 0x50(%rsp), %xmm5
    movdqu 0x60(%rsp), %xmm6
    movdqu 0x70(%rsp), %xmm7
    movdqu 0x80(%rsp), %xmm8
    movdqu 0x90(%rsp), %xmm9
    movdqu 0xA0(%rsp), %xmm10
    movdqu 0xB0(%rsp), %xmm11
    movdqu 0xC0(%rsp), %xmm12
    movdqu 0xD0(%rsp), %xmm13
    movdqu 0xE0(%rsp), %xmm14
    movdqu 0xF0(%rsp), %xmm15
    add $0x100, %rsp
lp_x86_64_restore_xmm_end:
    ret
