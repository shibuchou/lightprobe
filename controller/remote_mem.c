// remote_mem.c

#define _GNU_SOURCE

#include "controller_internal.h"

#include <sys/uio.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <stddef.h>

/*
 * remote_read
 *
 * 从目标进程读取内存   使用process_vm_readv()函数进行读取
 */
int remote_read(
    pid_t pid,
    uint64_t remote_addr,
    void *buf,
    size_t len)
{
    struct iovec local_iov;
    struct iovec remote_iov;

    ssize_t nread;

    
    if (pid <= 0 ||
        remote_addr == 0 ||
        buf == NULL ||
        len == 0) {

        errno = EINVAL;

        return -1;
    }

    
    local_iov.iov_base = buf;
    local_iov.iov_len  = len;

    
    remote_iov.iov_base = (void *)remote_addr;
    remote_iov.iov_len  = len;

    
    nread = process_vm_readv(
        pid,
        &local_iov,
        1,
        &remote_iov,
        1,
        0
    );

    if (nread < 0) {
        return -1;
    }

    
    if ((size_t)nread != len) {

        errno = EIO;

        return -1;
    }

    return 0;
}


/*
 * remote_write
 *
 * 向目标进程写内存，使用process_vm_writev()函数进行写入
 */
int remote_write(
    pid_t pid,
    uint64_t remote_addr,
    const void *buf,
    size_t len)
{
    struct iovec local_iov;
    struct iovec remote_iov;

    ssize_t nwritten;

    
    if (pid <= 0 ||
        remote_addr == 0 ||
        buf == NULL ||
        len == 0) {

        errno = EINVAL;

        return -1;
    }

    
    local_iov.iov_base = (void *)buf;
    local_iov.iov_len  = len;

    
    remote_iov.iov_base = (void *)remote_addr;
    remote_iov.iov_len  = len;

    
    nwritten = process_vm_writev(
        pid,
        &local_iov,
        1,
        &remote_iov,
        1,
        0
    );

    if (nwritten < 0) {
        return -1;
    }

    
    if ((size_t)nwritten != len) {

        errno = EIO;

        return -1;
    }

    return 0;
}