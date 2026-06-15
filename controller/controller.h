#ifndef LIGHTPROBE_CONTROLLER_H
#define LIGHTPROBE_CONTROLLER_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

int lp_attach_process(pid_t pid);
int lp_detach_process(pid_t pid);

int lp_stop_all_threads(pid_t pid);
int lp_resume_all_threads(pid_t pid);

int lp_find_library_base(pid_t pid, const char *lib_name, uint64_t *base_addr);
int lp_resolve_symbol(pid_t pid,
                      const char *lib_name,
                      const char *symbol_name,
                      uint64_t *runtime_addr);

int lp_remote_read(pid_t pid, uint64_t remote_addr, void *buf, size_t len);
int lp_remote_write(pid_t pid, uint64_t remote_addr, const void *buf, size_t len);
int lp_remote_mmap(pid_t pid, size_t size, int prot, uint64_t *remote_addr);

#endif

