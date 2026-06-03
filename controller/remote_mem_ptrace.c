#define _GNU_SOURCE

// remote_mem.c

#include "controller_internal.h"

#include <sys/ptrace.h>
#include <sys/uio.h>
#include <sys/user.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>










int remote_read(
    pid_t pid,
    uint64_t remote_addr,
    void *buf,
    size_t len)
{
    size_t i = 0;

    size_t word_size =
        sizeof(long);

    unsigned char *dst =
        (unsigned char *)buf;

    if (pid <= 0 ||
        remote_addr == 0 ||
        buf == NULL ||
        len == 0) {

        errno = EINVAL;

        return -1;
    }

    {
        struct iovec local = {
            .iov_base = buf,
            .iov_len = len,
        };
        struct iovec remote = {
            .iov_base = (void *)remote_addr,
            .iov_len = len,
        };
        ssize_t nread = process_vm_readv(pid, &local, 1, &remote, 1, 0);
        if (nread == (ssize_t)len) {
            return 0;
        }
        if (nread >= 0) {
            errno = EIO;
            return -1;
        }
    }

    /*
     * 按 word 读取
     */
    while (i < len) {

        long data;

        size_t copy_size;

        errno = 0;

        data = ptrace(
            PTRACE_PEEKDATA,
            pid,
            (void *)(remote_addr + i),
            NULL
        );

        if (data == -1 &&
            errno != 0) {

            return -1;
        }

        /*
         * 最后一次可能不足 word
         */
        copy_size = word_size;

        if (i + copy_size > len) {

            copy_size =
                len - i;
        }

        memcpy(
            dst + i,
            &data,
            copy_size
        );

        i += copy_size;
    }

    return 0;
}


/*
 * remote_write
 *
 * 使用 ptrace 向目标进程写内存
 */
int remote_write(
    pid_t pid,
    uint64_t remote_addr,
    const void *buf,
    size_t len)
{
    size_t i = 0;

    size_t word_size =
        sizeof(long);

    const unsigned char *src =
        (const unsigned char *)buf;

    if (pid <= 0 ||
        remote_addr == 0 ||
        buf == NULL ||
        len == 0) {

        errno = EINVAL;

        return -1;
    }

    {
        struct iovec local = {
            .iov_base = (void *)buf,
            .iov_len = len,
        };
        struct iovec remote = {
            .iov_base = (void *)remote_addr,
            .iov_len = len,
        };
        ssize_t nwritten = process_vm_writev(pid, &local, 1, &remote, 1, 0);
        if (nwritten == (ssize_t)len) {
            return 0;
        }
        if (nwritten >= 0) {
            errno = EIO;
            return -1;
        }
    }

    /*
     * 按 word 写入
     */
    while (i < len) {

        long data = 0;

        size_t copy_size;

        copy_size = word_size;

        /*
         * 最后不足一个 word
         */
        if (i + copy_size > len) {

            copy_size =
                len - i;

            /*
             * 必须先读原数据
             * 避免覆盖后续字节
             */
            errno = 0;

            data = ptrace(
                PTRACE_PEEKDATA,
                pid,
                (void *)(remote_addr + i),
                NULL
            );

            if (data == -1 &&
                errno != 0) {

                return -1;
            }
        }

        /*
         * 覆盖部分字节
         */
        memcpy(
            &data,
            src + i,
            copy_size
        );

        /*
         * 写回目标进程
         */
        if (ptrace(
                PTRACE_POKEDATA,
                pid,
                (void *)(remote_addr + i),
                (void *)data) < 0) {

            return -1;
        }

        i += copy_size;
    }

    return 0;
}




int remote_mmap(
    pid_t pid,
    size_t size,
    int prot,
    uint64_t *remote_addr)
{
    uint64_t retval = 0;

    if (pid <= 0 || size == 0 || remote_addr == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (remote_syscall(pid,
                       __NR_mmap,
                       0,
                       size,
                       (uint64_t)prot,
                       MAP_PRIVATE | MAP_ANONYMOUS,
                       (uint64_t)-1,
                       0,
                       &retval) < 0) {
        return -1;
    }

    if ((int64_t)retval < 0 && (int64_t)retval >= -4095) {
        errno = (int)-(int64_t)retval;
        return -1;
    }

    *remote_addr = retval;
    return 0;
}

int remote_munmap(
    pid_t pid,
    uint64_t remote_addr,
    size_t size)
{
    uint64_t retval = 0;

    if (pid <= 0 || remote_addr == 0 || size == 0) {
        errno = EINVAL;
        return -1;
    }

    if (remote_syscall(
            pid,
            __NR_munmap,
            remote_addr,
            size,
            0,
            0,
            0,
            0,
            &retval) < 0) {
        return -1;
    }

    if ((int64_t)retval < 0 && (int64_t)retval >= -4095) {
        errno = (int)-(int64_t)retval;
        return -1;
    }

    return 0;
}
