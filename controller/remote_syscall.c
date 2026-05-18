#include "controller_internal.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <errno.h>

#include <unistd.h>
#include <sys/ptrace.h>
#include <sys/user.h>

#include <sys/wait.h>

#include <sys/mman.h>
#include <sys/syscall.h>



#define INT3_OPCODE 0xCC




static int find_syscall_gadget(
    pid_t pid,
    uint64_t *gadget_addr)
{
    uint8_t pattern[] = {
        0x0f,
        0x05,
        0xc3
    };

    return resolve_gadget_runtime(
        pid,
        "libc.so",
        pattern,
        sizeof(pattern),
        gadget_addr);
}




int remote_syscall(
    pid_t pid,
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

    uint64_t gadget_addr;

    uint64_t original_rip;

    uint8_t original_code;

    uint64_t fake_ret;

    int status;


    if (retval == NULL) {
        errno = EINVAL;
        return -1;
    }


    
    if (find_syscall_gadget(
            pid,
            &gadget_addr) < 0) {

        return -1;
    }


    
    if (ptrace(
            PTRACE_GETREGS,
            pid,
            NULL,
            &saved_regs) < 0) {

        return -1;
    }

    regs = saved_regs;


    original_rip = regs.rip;


    
    if (remote_read(
            pid,
            original_rip,
            &original_code,
            1) < 0) {

        return -1;
    }


    
    {
        uint8_t int3 = INT3_OPCODE;

        if (remote_write(
                pid,
                original_rip,
                &int3,
                1) < 0) {

            return -1;
        }
    }


    
    fake_ret = original_rip;

    regs.rsp -= sizeof(uint64_t);

    if (remote_write(
            pid,
            regs.rsp,
            &fake_ret,
            sizeof(fake_ret)) < 0) {

        return -1;
    }


    
    regs.rax = syscall_no;

    regs.rdi = arg1;
    regs.rsi = arg2;
    regs.rdx = arg3;

    regs.r10 = arg4;
    regs.r8  = arg5;
    regs.r9  = arg6;


    
    regs.rip = gadget_addr;


    
    if (ptrace(
            PTRACE_SETREGS,
            pid,
            NULL,
            &regs) < 0) {

        return -1;
    }


    
    if (ptrace(
            PTRACE_CONT,
            pid,
            NULL,
            NULL) < 0) {

        return -1;
    }


    
    waitpid(pid,
            &status,
            0);


    if (!WIFSTOPPED(status) ||
        WSTOPSIG(status) != SIGTRAP) {

        errno = EFAULT;

        return -1;
    }


    
    if (ptrace(
            PTRACE_GETREGS,
            pid,
            NULL,
            &regs) < 0) {

        return -1;
    }


    *retval = regs.rax;


    
    if (remote_write(
            pid,
            original_rip,
            &original_code,
            1) < 0) {

        return -1;
    }


    
    if (ptrace(
            PTRACE_SETREGS,
            pid,
            NULL,
            &saved_regs) < 0) {

        return -1;
    }


    return 0;
}