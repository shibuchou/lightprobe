#define _GNU_SOURCE

#include "controller.h"

#include <errno.h>
#include <string.h>
#include <sys/uio.h>

int lp_attach_process(pid_t pid)
{
    (void)pid;
    errno = ENOSYS;
    return -1;
}

int lp_detach_process(pid_t pid)
{
    (void)pid;
    errno = ENOSYS;
    return -1;
}

int lp_stop_all_threads(pid_t pid)
{
    (void)pid;
    errno = ENOSYS;
    return -1;
}

int lp_resume_all_threads(pid_t pid)
{
    (void)pid;
    errno = ENOSYS;
    return -1;
}

int lp_find_library_base(pid_t pid, const char *lib_name, uint64_t *base_addr)
{
    (void)pid;
    (void)lib_name;
    (void)base_addr;
    errno = ENOSYS;
    return -1;
}

int lp_resolve_symbol(pid_t pid,
                      const char *lib_name,
                      const char *symbol_name,
                      uint64_t *runtime_addr)
{
    (void)pid;
    (void)lib_name;
    (void)symbol_name;
    (void)runtime_addr;
    errno = ENOSYS;
    return -1;
}

int lp_remote_read(pid_t pid, uint64_t remote_addr, void *buf, size_t len)
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
    if (nread < 0) {
        return -1;
    }
    if ((size_t)nread != len) {
        errno = EIO;
        return -1;
    }
    return 0;
}

int lp_remote_write(pid_t pid, uint64_t remote_addr, const void *buf, size_t len)
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
    if (nwritten < 0) {
        return -1;
    }
    if ((size_t)nwritten != len) {
        errno = EIO;
        return -1;
    }
    return 0;
}

int lp_remote_mmap(pid_t pid, size_t size, int prot, uint64_t *remote_addr)
{
    (void)pid;
    (void)size;
    (void)prot;
    (void)remote_addr;
    errno = ENOSYS;
    return -1;
}
