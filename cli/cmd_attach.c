#include "cli.h"
#include "lightprobe.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_attach_args(int argc, char **argv, struct lp_probe_spec *spec)
{
    memset(spec, 0, sizeof(*spec));

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--pid") == 0 && i + 1 < argc) {
            spec->pid = (pid_t)strtol(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--lib") == 0 && i + 1 < argc) {
            spec->lib_name = argv[++i];
        } else if (strcmp(argv[i], "--func") == 0 && i + 1 < argc) {
            spec->func_name = argv[++i];
        } else if (strcmp(argv[i], "--ret") == 0) {
            spec->has_retprobe = 1;
        } else {
            return -1;
        }
    }

    if (spec->pid <= 0 || spec->lib_name == NULL || spec->func_name == NULL) {
        return -1;
    }
    return 0;
}

int lp_cmd_attach(int argc, char **argv)
{
    struct lp_probe_spec spec;
    struct probe_desc desc;

    if (parse_attach_args(argc, argv, &spec) < 0) {
        lp_print_usage("lightprobe");
        return 2;
    }

    if (lp_probe_attach(&spec, &desc) < 0) {
        fprintf(stderr, "attach failed: %s\n", strerror(errno));
        return 1;
    }

    printf("attached probe_id=%d pid=%d %s:%s target=0x%lx ret=%d\n",
           desc.probe_id, desc.pid, desc.lib_name, desc.func_name,
           desc.target_addr, desc.has_retprobe);
    return 0;
}

