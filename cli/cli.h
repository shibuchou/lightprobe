#ifndef LIGHTPROBE_CLI_H
#define LIGHTPROBE_CLI_H

int lp_cmd_attach(int argc, char **argv);
int lp_cmd_detach(int argc, char **argv);
int lp_cmd_enable(int argc, char **argv);
int lp_cmd_disable(int argc, char **argv);
int lp_cmd_events(int argc, char **argv);
int lp_cmd_list(int argc, char **argv);
void lp_print_usage(const char *prog);

#endif
