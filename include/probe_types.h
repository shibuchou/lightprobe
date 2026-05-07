#ifndef LIGHTPROBE_PROBE_TYPES_H
#define LIGHTPROBE_PROBE_TYPES_H

#include <stdint.h>
#include <sys/types.h>

#define LP_MAX_PROBES 16
#define LP_MAX_ORIGINAL_CODE 32
#define LP_MAX_NAME 128

enum lp_probe_state {
    LP_PROBE_UNUSED = 0,
    LP_PROBE_ATTACHED = 1,
    LP_PROBE_ENABLED = 2,
};

struct probe_desc {
    int probe_id;
    pid_t pid;

    uint64_t target_addr;
    uint64_t trampoline_addr;
    uint64_t entry_stub_addr;
    uint64_t ret_stub_addr;
    uint64_t config_addr;
    uint64_t event_buffer_addr;
    uint64_t shadow_stack_addr;
    uint64_t runtime_base_addr;
    uint64_t runtime_size;

    unsigned char original_code[LP_MAX_ORIGINAL_CODE];
    int original_len;

    int enabled;
    int has_retprobe;

    char lib_name[LP_MAX_NAME];
    char func_name[LP_MAX_NAME];
};

struct lp_probe_spec {
    pid_t pid;
    const char *lib_name;
    const char *func_name;
    int has_retprobe;
};

#endif
