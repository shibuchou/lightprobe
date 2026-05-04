#ifndef LIGHTPROBE_RUNTIME_H
#define LIGHTPROBE_RUNTIME_H

#include <stdint.h>

#include "event.h"
#include "probe_types.h"

#define LP_EVENT_BUFFER_CAPACITY 4096
#define LP_SHADOW_STACK_DEPTH 64

struct lp_runtime_config {
    uint32_t enabled;
    uint32_t probe_id;
    uint64_t event_buffer_addr;
    uint64_t shadow_stack_addr;
};

struct lp_event_buffer {
    volatile uint64_t write_index;
    uint64_t capacity;
    struct probe_event events[LP_EVENT_BUFFER_CAPACITY];
};

struct lp_shadow_stack_entry {
    uint32_t tid;
    uint32_t depth;
    uint64_t return_addrs[LP_SHADOW_STACK_DEPTH];
    uint64_t entry_timestamps[LP_SHADOW_STACK_DEPTH];
};

int lp_runtime_config_init(struct lp_runtime_config *config, const struct probe_desc *desc);
void lp_runtime_config_set_enabled(struct lp_runtime_config *config, int enabled);
void lp_event_buffer_init(struct lp_event_buffer *buffer);

#endif

