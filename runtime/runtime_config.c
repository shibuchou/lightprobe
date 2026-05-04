#include "runtime.h"

int lp_runtime_config_init(struct lp_runtime_config *config, const struct probe_desc *desc)
{
    config->enabled = desc->enabled ? 1U : 0U;
    config->probe_id = (uint32_t)desc->probe_id;
    config->event_buffer_addr = 0;
    config->shadow_stack_addr = 0;
    return 0;
}

void lp_runtime_config_set_enabled(struct lp_runtime_config *config, int enabled)
{
    config->enabled = enabled ? 1U : 0U;
}

