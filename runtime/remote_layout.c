#include "runtime.h"

#include <errno.h>
#include <string.h>

static uint64_t align_up_u64(uint64_t value, uint64_t align)
{
    return (value + align - 1U) & ~(align - 1U);
}

int lp_remote_runtime_layout_init(struct lp_remote_runtime_layout *layout, uint64_t base)
{
    if (layout == NULL) {
        errno = EINVAL;
        return -1;
    }

    memset(layout, 0, sizeof(*layout));
    layout->base = base;

    uint64_t offset = 0;

    layout->config_offset = offset;
    offset += sizeof(struct lp_runtime_config);
    offset = align_up_u64(offset, LP_REMOTE_RUNTIME_ALIGN);

    layout->event_buffer_offset = offset;
    offset += sizeof(struct lp_event_buffer);
    offset = align_up_u64(offset, LP_REMOTE_RUNTIME_ALIGN);

    layout->shadow_stack_offset = offset;
    offset += sizeof(struct lp_shadow_stack);
    offset = align_up_u64(offset, LP_REMOTE_RUNTIME_ALIGN);

    layout->trampoline_offset = offset;
    offset += LP_TRAMPOLINE_SIZE;
    offset = align_up_u64(offset, LP_REMOTE_RUNTIME_ALIGN);

    layout->entry_stub_offset = offset;
    offset += LP_REMOTE_STUB_SIZE;
    offset = align_up_u64(offset, LP_REMOTE_RUNTIME_ALIGN);

    layout->ret_stub_offset = offset;
    offset += LP_REMOTE_STUB_SIZE;
    offset = align_up_u64(offset, LP_REMOTE_RUNTIME_ALIGN);

    layout->size = offset;
    layout->config_addr = base + layout->config_offset;
    layout->event_buffer_addr = base + layout->event_buffer_offset;
    layout->shadow_stack_addr = base + layout->shadow_stack_offset;
    layout->trampoline_addr = base + layout->trampoline_offset;
    layout->entry_stub_addr = base + layout->entry_stub_offset;
    layout->ret_stub_addr = base + layout->ret_stub_offset;

    return 0;
}

void lp_probe_desc_set_runtime_layout(struct probe_desc *desc,
                                      const struct lp_remote_runtime_layout *layout)
{
    desc->runtime_base_addr = layout->base;
    desc->runtime_size = layout->size;
    desc->config_addr = layout->config_addr;
    desc->event_buffer_addr = layout->event_buffer_addr;
    desc->shadow_stack_addr = layout->shadow_stack_addr;
    desc->trampoline_addr = layout->trampoline_addr;
    desc->entry_stub_addr = layout->entry_stub_addr;
    desc->ret_stub_addr = layout->ret_stub_addr;
}

