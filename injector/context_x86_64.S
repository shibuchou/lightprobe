# NOTE: The actual register context save/restore logic lives in trampoline_x86_64.c
# (inline assembly), NOT here. This .S file provides helper symbols/labels that the
# trampoline code references. The separation exists because the trampoline needs
# dynamic address computation that is easier to express in C with inline asm.
.text
.globl lp_context_x86_64_placeholder
.type lp_context_x86_64_placeholder, @function
lp_context_x86_64_placeholder:
    ret

