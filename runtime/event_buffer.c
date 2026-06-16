#include "runtime.h"

#include <string.h>

void lp_event_buffer_init(struct lp_event_buffer *buffer)
{
    memset(buffer, 0, sizeof(*buffer));
    buffer->capacity = LP_EVENT_BUFFER_CAPACITY;
}

