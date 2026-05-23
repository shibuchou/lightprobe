
#define _GNU_SOURCE

#include "controller.h"
#include "controller_internal.h"

#include <errno.h>
#include <stdint.h>
#include <string.h>


int lp_attach_process(pid_t pid)
{
    return process_attach(pid);
}



int lp_detach_process(pid_t pid)
{
    return process_detach(pid);
}



int lp_stop_all_threads(pid_t pid)
{
    return stop_all_threads(pid);
}



int lp_resume_all_threads(pid_t pid)
{
    return resume_all_threads(pid);
}



int lp_find_library_base(
    pid_t pid,
    const char *lib_name,
    uint64_t *base_addr)
{
    char path_buf[512];

    if (lib_name == NULL ||
        base_addr == NULL) {

        errno = EINVAL;

        return -1;
    }

    return find_library_base(
        pid,
        lib_name,
        base_addr,
        path_buf,
        sizeof(path_buf)
    );
}



int lp_resolve_symbol(
    pid_t pid,
    const char *lib_name,
    const char *symbol_name,
    uint64_t *runtime_addr)
{
    uint64_t base_addr;

    char path_buf[512];

    if (lib_name == NULL ||
        symbol_name == NULL ||
        runtime_addr == NULL) {

        errno = EINVAL;

        return -1;
    }

    
    if (find_library_base(
            pid,
            lib_name,
            &base_addr,
            path_buf,
            sizeof(path_buf)) < 0) {

        return -1;
    }
    

    return resolve_symbol(
        path_buf,
        symbol_name,
        base_addr,
        runtime_addr
    );
}



int lp_remote_read(
    pid_t pid,
    uint64_t remote_addr,
    void *buf,
    size_t len)
{
    return remote_read(
        pid,
        remote_addr,
        buf,
        len
    );
}



int lp_remote_write(
    pid_t pid,
    uint64_t remote_addr,
    const void *buf,
    size_t len)
{
    return remote_write(
        pid,
        remote_addr,
        buf,
        len
    );
}


int lp_remote_mmap(
    pid_t pid,
    size_t size,
    int prot,
    uint64_t *remote_addr)
{
    return remote_mmap(
        pid,
        size,
        prot,
        remote_addr
    );
}

int lp_remote_munmap(
    pid_t pid,
    uint64_t remote_addr,
    size_t size)
{
    return remote_munmap(
        pid,
        remote_addr,
        size
    );
}
