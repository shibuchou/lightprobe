#include "cli.h"
#include "lightprobe.h"

#include <stdio.h>

int lp_cmd_list(int argc, char **argv)
{
    (void)argv;

    if (argc != 1) {
        lp_print_usage("lightprobe");
        return 2;
    }

    printf("ID  PID      ENABLED  RET  TARGET              LIB:FUNC\n");
    for (int i = 0; i < LP_MAX_PROBES; i++) {
        const struct probe_desc *desc = lp_probe_manager_at(i);
        if (desc == NULL || desc->pid == 0) {
            continue;
        }

        printf("%-3d %-8d %-8d %-4d 0x%016lx  %s:%s\n",
               desc->probe_id,
               desc->pid,
               desc->enabled,
               desc->has_retprobe,
               desc->target_addr,
               desc->lib_name,
               desc->func_name);
    }

    return 0;
}
