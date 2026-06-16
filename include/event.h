#ifndef LIGHTPROBE_EVENT_H
#define LIGHTPROBE_EVENT_H

#include <stdint.h>

enum probe_event_type {
    PROBE_EVENT_ENTRY = 1,
    PROBE_EVENT_RETURN = 2,
};

struct probe_event {
    uint64_t timestamp_ns;
    uint32_t pid;
    uint32_t tid;
    uint32_t probe_id;
    uint32_t event_type;

    uint64_t args[6];
    uint64_t retval;

    uint64_t duration_ns;
};

#endif

