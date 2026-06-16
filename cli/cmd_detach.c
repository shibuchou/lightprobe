#include "cli.h"
#include "lightprobe.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_pid_func(int argc, char **argv, pid_t *pid, const char **func)
{
    *pid = 0;
    *func = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--pid") == 0 && i + 1 < argc) {
            *pid = (pid_t)strtol(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--func") == 0 && i + 1 < argc) {
            *func = argv[++i];
        } else {
            return -1;
        }
    }
    return (*pid > 0 && *func != NULL) ? 0 : -1;
}

int lp_cmd_detach(int argc, char **argv)
{
    pid_t pid;
    const char *func;
    if (parse_pid_func(argc, argv, &pid, &func) < 0) {
        lp_print_usage("lightprobe");
        return 2;
    }

    struct probe_desc *desc = lp_probe_manager_find(pid, func);
    if (desc == NULL) {
        fprintf(stderr, "detach failed: probe not found\n");
        return 1;
    }

    if (lp_probe_detach(desc) < 0) {
        fprintf(stderr, "detach failed: %s\n", strerror(errno));
        return 1;
    }

    printf("detached pid=%d func=%s\n", pid, func);
    return 0;
}

