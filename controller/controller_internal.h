

#ifndef CONTROLLER_INTERNAL_H
#define CONTROLLER_INTERNAL_H

#include <stdint.h>
#include <sys/types.h>
#include <stddef.h>


/* process_attach.c */

int process_attach(pid_t pid);
int process_detach(pid_t pid);


/* thread_control.c */

int stop_all_threads(pid_t pid);
int resume_all_threads(pid_t pid);


/* maps_parser.c */

int find_library_base(
    pid_t pid,
    const char *lib_name,
    uint64_t *base_addr,
    char *path_buf,
    size_t path_len
);


/* elf_resolver.c */

int resolve_symbol(
    const char *elf_path,
    const char *symbol_name,
    uint64_t base_addr,
    uint64_t *runtime_addr
);


/* remote_mem.c */

int remote_read(
    pid_t pid,
    uint64_t remote_addr,
    void *buf,
    size_t len
);

int remote_write(
    pid_t pid,
    uint64_t remote_addr,
    const void *buf,
    size_t len
);

int remote_mmap(pid_t pid,
                size_t size,
                int prot,
                uint64_t *remote_addr);

#endif