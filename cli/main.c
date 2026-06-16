#include "cli.h"
#include "lightprobe.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

void lp_print_usage(const char *prog)
{
    fprintf(stderr,
            "Usage:\n"
            "  %s attach --pid <pid> --lib <lib> --func <func> [--addr <addr>] [--ret]\n"
            "  %s detach --pid <pid> --func <func>\n"
            "  %s enable --pid <pid> --func <func>\n"
            "  %s disable --pid <pid> --func <func>\n"
            "  %s events --pid <pid> [--func <func>] [--limit <n>] [--csv]\n"
            "  %s list\n",
            prog, prog, prog, prog, prog, prog);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        lp_print_usage(argv[0]);
        return 2;
    }

    if (lp_probe_manager_load() < 0) {
        fprintf(stderr, "failed to load probe state: %s\n", strerror(errno));
        return 1;
    }

    if (strcmp(argv[1], "attach") == 0) {
        return lp_cmd_attach(argc - 1, argv + 1);
    }
    if (strcmp(argv[1], "detach") == 0) {
        return lp_cmd_detach(argc - 1, argv + 1);
    }
    if (strcmp(argv[1], "enable") == 0) {
        return lp_cmd_enable(argc - 1, argv + 1);
    }
    if (strcmp(argv[1], "disable") == 0) {
        return lp_cmd_disable(argc - 1, argv + 1);
    }
    if (strcmp(argv[1], "events") == 0) {
        return lp_cmd_events(argc - 1, argv + 1);
    }
    if (strcmp(argv[1], "list") == 0) {
        return lp_cmd_list(argc - 1, argv + 1);
    }

    lp_print_usage(argv[0]);
    return 2;
}
