

#include "controller_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>


int find_library_base(pid_t pid,
                      const char *lib_name,
                      uint64_t *base_out,
                      char *path_out,
                      size_t path_len)
{
    FILE *fp = NULL;

    char maps_path[64];
    char line[1024];

    if (pid <= 0 ||
        lib_name == NULL ||
        base_out == NULL) {

        errno = EINVAL;
        return -1;
    }

    
    snprintf(maps_path,
             sizeof(maps_path),
             "/proc/%d/maps",
             pid);

    fp = fopen(maps_path, "r");
    if (fp == NULL) {
        return -1;
    }

    
    while (fgets(line, sizeof(line), fp) != NULL) {

        uint64_t start = 0;
        uint64_t end = 0;
        uint64_t offset = 0;

        char perms[8] = {0};
        char dev[16] = {0};

        unsigned long inode = 0;

        char pathname[512] = {0};

        
        int fields = sscanf(
            line,
            "%" SCNx64 "-%" SCNx64
            " %7s %"
            SCNx64
            " %15s %lu %511s",
            &start,
            &end,
            perms,
            &offset,
            dev,
            &inode,
            pathname
        );

        
        if (fields < 7) {
            continue;
        }

        
        if (offset != 0) {
            continue;
        }

        
        if (strstr(pathname, lib_name) == NULL) {
            continue;
        }

        
        *base_out = start;

        
        if (path_out != NULL &&
            path_len > 0) {

            strncpy(path_out,
                    pathname,
                    path_len - 1);

            path_out[path_len - 1] = '\0';
            path_out[strcspn(path_out, "\n")] = '\0';
        }

        fclose(fp);

        return 0;
    }

    fclose(fp);

    errno = ENOENT;

    return -1;
}