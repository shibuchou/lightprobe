#include "runtime.h"

#include <stddef.h>

int lp_runtime_config_init(struct lp_runtime_config *config, const struct probe_desc *desc)
{
    config->enabled = desc->enabled ? 1U : 0U;
    config->probe_id = (uint32_t)desc->probe_id;
    config->event_buffer_addr = desc->event_buffer_addr;
    config->shadow_stack_addr = desc->shadow_stack_addr;
    return 0;
}

void lp_runtime_config_set_enabled(struct lp_runtime_config *config, int enabled)
{
    config->enabled = enabled ? 1U : 0U;
}

uint64_t lp_runtime_config_enabled_addr(const struct probe_desc *desc)
{
    return desc->config_addr + offsetof(struct lp_runtime_config, enabled);
}
