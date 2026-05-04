#include "cli.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

int lp_cmd_events(int argc, char **argv)
{
    pid_t pid = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--pid") == 0 && i + 1 < argc) {
            pid = (pid_t)strtol(argv[++i], NULL, 10);
        } else {
            lp_print_usage("lightprobe");
            return 2;
        }
    }

    if (pid <= 0) {
        lp_print_usage("lightprobe");
        return 2;
    }

    printf("events command is reserved for runtime event buffer reader, pid=%d\n", pid);
    return 0;
}

