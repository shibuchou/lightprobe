#ifndef LIGHTPROBE_RUNTIME_H
#define LIGHTPROBE_RUNTIME_H

#include <stdint.h>

#include "event.h"
#include "probe_types.h"

#define LP_EVENT_BUFFER_CAPACITY 4096
#define LP_SHADOW_STACK_DEPTH 64
#define LP_SHADOW_STACK_THREADS 256
#define LP_REMOTE_STUB_SIZE 4096
#define LP_TRAMPOLINE_SIZE 128
#define LP_REMOTE_RUNTIME_ALIGN 16

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

struct lp_shadow_stack {
    struct lp_shadow_stack_entry entries[LP_SHADOW_STACK_THREADS];
};

struct lp_remote_runtime_layout {
    uint64_t base;
    uint64_t size;

    uint64_t config_addr;
    uint64_t event_buffer_addr;
    uint64_t shadow_stack_addr;
    uint64_t trampoline_addr;
    uint64_t entry_stub_addr;
    uint64_t ret_stub_addr;

    uint64_t config_offset;
    uint64_t event_buffer_offset;
    uint64_t shadow_stack_offset;
    uint64_t trampoline_offset;
    uint64_t entry_stub_offset;
    uint64_t ret_stub_offset;
};

int lp_runtime_config_init(struct lp_runtime_config *config, const struct probe_desc *desc);
void lp_runtime_config_set_enabled(struct lp_runtime_config *config, int enabled);
uint64_t lp_runtime_config_enabled_addr(const struct probe_desc *desc);
void lp_event_buffer_init(struct lp_event_buffer *buffer);
void lp_shadow_stack_init(struct lp_shadow_stack *stack);

int lp_remote_runtime_layout_init(struct lp_remote_runtime_layout *layout, uint64_t base);
void lp_probe_desc_set_runtime_layout(struct probe_desc *desc,
                                      const struct lp_remote_runtime_layout *layout);

#endif
