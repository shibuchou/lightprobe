#include "controller_internal.h"

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <sys/ptrace.h>
#include <sys/syscall.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <unistd.h>

#define INT3_OPCODE 0xCC

static int find_syscall_gadget(pid_t pid, uint64_t *gadget_addr)
{
    static const uint8_t pattern[] = {0x0f, 0x05, 0xc3};

    return resolve_gadget_runtime(pid,
                                  "libc.so",
                                  pattern,
                                  sizeof(pattern),
                                  gadget_addr);
}

static void restore_remote_state_best_effort(pid_t pid,
                                             uint64_t patched_addr,
                                             const uint8_t *saved_code,
                                             size_t saved_code_len,
                                             const struct user_regs_struct *saved_regs)
{
    if (patched_addr != 0 && saved_code != NULL && saved_code_len > 0) {
        (void)remote_write(pid, patched_addr, saved_code, saved_code_len);
    }
    if (saved_regs != NULL) {
        (void)ptrace(PTRACE_SETREGS, pid, NULL, (void *)saved_regs);
    }
}

int remote_syscall(pid_t pid,
                   long syscall_no,
                   uint64_t arg1,
                   uint64_t arg2,
                   uint64_t arg3,
                   uint64_t arg4,
                   uint64_t arg5,
                   uint64_t arg6,
                   uint64_t *retval)
{
    struct user_regs_struct regs;
    struct user_regs_struct saved_regs;
    uint64_t gadget_addr = 0;
    uint64_t original_rip = 0;
    uint64_t fake_ret = 0;
    uint8_t original_code = 0;
    uint8_t int3 = INT3_OPCODE;
    int have_regs = 0;
    int have_code = 0;
    int status = 0;

    if (pid <= 0 || retval == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (find_syscall_gadget(pid, &gadget_addr) < 0) {
        return -1;
    }

    if (ptrace(PTRACE_GETREGS, pid, NULL, &saved_regs) < 0) {
        return -1;
    }
    have_regs = 1;
    regs = saved_regs;
    original_rip = regs.rip;

    if (remote_read(pid, original_rip, &original_code, sizeof(original_code)) < 0) {
        return -1;
    }
    have_code = 1;

    if (remote_write(pid, original_rip, &int3, sizeof(int3)) < 0) {
        int saved_errno = errno;
        restore_remote_state_best_effort(pid,
                                         have_code ? original_rip : 0,
                                         have_code ? &original_code : NULL,
                                         have_code ? sizeof(original_code) : 0,
                                         have_regs ? &saved_regs : NULL);
        errno = saved_errno;
        return -1;
    }

    fake_ret = original_rip;
    regs.rsp -= sizeof(uint64_t);
    if (remote_write(pid, regs.rsp, &fake_ret, sizeof(fake_ret)) < 0) {
        int saved_errno = errno;
        restore_remote_state_best_effort(pid, original_rip, &original_code,
                                         sizeof(original_code), &saved_regs);
        errno = saved_errno;
        return -1;
    }

    regs.rax = (uint64_t)syscall_no;
    regs.rdi = arg1;
    regs.rsi = arg2;
    regs.rdx = arg3;
    regs.r10 = arg4;
    regs.r8 = arg5;
    regs.r9 = arg6;
    regs.rip = gadget_addr;

    if (ptrace(PTRACE_SETREGS, pid, NULL, &regs) < 0) {
        int saved_errno = errno;
        restore_remote_state_best_effort(pid, original_rip, &original_code,
                                         sizeof(original_code), &saved_regs);
        errno = saved_errno;
        return -1;
    }

    if (ptrace(PTRACE_CONT, pid, NULL, NULL) < 0) {
        int saved_errno = errno;
        restore_remote_state_best_effort(pid, original_rip, &original_code,
                                         sizeof(original_code), &saved_regs);
        errno = saved_errno;
        return -1;
    }

    if (waitpid(pid, &status, __WALL) < 0) {
        int saved_errno = errno;
        restore_remote_state_best_effort(pid, original_rip, &original_code,
                                         sizeof(original_code), &saved_regs);
        errno = saved_errno;
        return -1;
    }

    if (!WIFSTOPPED(status) || WSTOPSIG(status) != SIGTRAP) {
        restore_remote_state_best_effort(pid, original_rip, &original_code,
                                         sizeof(original_code), &saved_regs);
        errno = EFAULT;
        return -1;
    }

    if (ptrace(PTRACE_GETREGS, pid, NULL, &regs) < 0) {
        int saved_errno = errno;
        restore_remote_state_best_effort(pid, original_rip, &original_code,
                                         sizeof(original_code), &saved_regs);
        errno = saved_errno;
        return -1;
    }
    *retval = regs.rax;

    if (remote_write(pid, original_rip, &original_code, sizeof(original_code)) < 0) {
        int saved_errno = errno;
        restore_remote_state_best_effort(pid, 0, NULL, 0, &saved_regs);
        errno = saved_errno;
        return -1;
    }

    if (ptrace(PTRACE_SETREGS, pid, NULL, &saved_regs) < 0) {
        return -1;
    }

    return 0;
}
