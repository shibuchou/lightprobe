#include "controller_internal.h"

#include <stdio.h>
#include <stdlib.h>

#include <stdint.h>
#include <stddef.h>

#include <string.h>

#include <fcntl.h>
#include <unistd.h>

#include <sys/stat.h>
#include <errno.h>




static int find_gadget_offset(
    const char *elf_path,
    const uint8_t *pattern,
    size_t pattern_len,
    uint64_t *offset_out)
{
    int fd;

    struct stat st;

    uint8_t *buf = NULL;

    ssize_t nread;

    size_t i;


    if (elf_path == NULL ||
        pattern == NULL ||
        pattern_len == 0 ||
        offset_out == NULL) {

        errno = EINVAL;

        return -1;
    }


    fd = open(elf_path, O_RDONLY);
    if (fd < 0) {
        return -1;
    }


    if (fstat(fd, &st) < 0) {

        close(fd);

        return -1;
    }


    buf = (uint8_t *)malloc(st.st_size);
    if (buf == NULL) {

        close(fd);

        return -1;
    }


    nread = read(fd,
                 buf,
                 st.st_size);

    if (nread != st.st_size) {

        free(buf);

        close(fd);

        return -1;
    }


    for (i = 0;
         i <= (size_t)st.st_size - pattern_len;
         i++) {

        if (memcmp(buf + i,
                   pattern,
                   pattern_len) == 0) {

            *offset_out = i;

            free(buf);

            close(fd);

            return 0;
        }
    }


    free(buf);

    close(fd);

    errno = ENOENT;

    return -1;
}




int resolve_gadget_runtime(
    pid_t pid,
    const char *module_name,
    const uint8_t *pattern,
    size_t pattern_len,
    uint64_t *runtime_addr)
{
    uint64_t base = 0;

    uint64_t offset = 0;

    char path[512] = {0};


    if (pid <= 0 ||
        module_name == NULL ||
        pattern == NULL ||
        pattern_len == 0 ||
        runtime_addr == NULL) {

        errno = EINVAL;

        return -1;
    }


    
    if (find_library_base(
            pid,
            module_name,
            &base,
            path,
            sizeof(path)) < 0) {

        return -1;
    }


    
    if (find_gadget_offset(
            path,
            pattern,
            pattern_len,
            &offset) < 0) {

        return -1;
    }


    
    *runtime_addr =
        base + offset;

    return 0;
}