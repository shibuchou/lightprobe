#include "cli.h"

#include "controller.h"
#include "lightprobe.h"
#include "runtime.h"

#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

struct event_args {
    pid_t pid;
    const char *func;
    uint64_t limit;
    int csv;
};

static const char *event_type_name(uint32_t event_type)
{
    switch (event_type) {
    case PROBE_EVENT_ENTRY:
        return "entry";
    case PROBE_EVENT_RETURN:
        return "return";
    default:
        return "unknown";
    }
}

static int parse_events_args(int argc, char **argv, struct event_args *args)
{
    memset(args, 0, sizeof(*args));
    args->limit = 32;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--pid") == 0 && i + 1 < argc) {
            args->pid = (pid_t)strtol(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--func") == 0 && i + 1 < argc) {
            args->func = argv[++i];
        } else if (strcmp(argv[i], "--limit") == 0 && i + 1 < argc) {
            args->limit = strtoull(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--csv") == 0) {
            args->csv = 1;
        } else {
            return -1;
        }
    }

    if (args->pid <= 0 || args->limit == 0) {
        return -1;
    }
    if (args->limit > LP_EVENT_BUFFER_CAPACITY) {
        args->limit = LP_EVENT_BUFFER_CAPACITY;
    }
    return 0;
}

static const struct probe_desc *find_first_probe_by_pid(pid_t pid)
{
    for (int i = 0; i < LP_MAX_PROBES; i++) {
        const struct probe_desc *desc = lp_probe_manager_at(i);
        if (desc != NULL && desc->pid == pid) {
            return desc;
        }
    }
    return NULL;
}

static void print_event_human(const struct probe_event *event)
{
    printf("ts=%lu pid=%u tid=%u probe=%u type=%s "
           "args=[0x%lx,0x%lx,0x%lx,0x%lx,0x%lx,0x%lx] "
           "retval=0x%lx duration=%lu\n",
           event->timestamp_ns,
           event->pid,
           event->tid,
           event->probe_id,
           event_type_name(event->event_type),
           event->args[0],
           event->args[1],
           event->args[2],
           event->args[3],
           event->args[4],
           event->args[5],
           event->retval,
           event->duration_ns);
}

static void print_event_csv_header(void)
{
    printf("timestamp_ns,pid,tid,probe_id,event_type,arg1,arg2,arg3,arg4,arg5,arg6,retval,duration_ns\n");
}

static void print_event_csv(const struct probe_event *event)
{
    printf("%lu,%u,%u,%u,%u,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu\n",
           event->timestamp_ns,
           event->pid,
           event->tid,
           event->probe_id,
           event->event_type,
           event->args[0],
           event->args[1],
           event->args[2],
           event->args[3],
           event->args[4],
           event->args[5],
           event->retval,
           event->duration_ns);
}

static int read_and_print_events(const struct probe_desc *desc, const struct event_args *args)
{
    struct lp_event_buffer header;
    struct probe_event event;

    if (desc->event_buffer_addr == 0) {
        errno = ENOENT;
        return -1;
    }

    if (lp_remote_read(desc->pid, desc->event_buffer_addr, &header,
                       offsetof(struct lp_event_buffer, events)) < 0) {
        return -1;
    }

    if (header.capacity == 0 || header.capacity > LP_EVENT_BUFFER_CAPACITY) {
        errno = EINVAL;
        return -1;
    }

    uint64_t total = header.write_index;
    uint64_t count = total < args->limit ? total : args->limit;
    uint64_t start = total > count ? total - count : 0;

    if (args->csv) {
        print_event_csv_header();
    } else {
        printf("probe_id=%d pid=%d %s:%s event_buffer=0x%lx total=%lu showing=%lu\n",
               desc->probe_id,
               desc->pid,
               desc->lib_name,
               desc->func_name,
               desc->event_buffer_addr,
               total,
               count);
    }

    for (uint64_t seq = start; seq < total; seq++) {
        uint64_t slot = seq % header.capacity;
        uint64_t event_addr = desc->event_buffer_addr +
                              offsetof(struct lp_event_buffer, events) +
                              slot * sizeof(struct probe_event);
        if (lp_remote_read(desc->pid, event_addr, &event, sizeof(event)) < 0) {
            return -1;
        }
        if (args->csv) {
            print_event_csv(&event);
        } else {
            print_event_human(&event);
        }
    }

    return 0;
}

int lp_cmd_events(int argc, char **argv)
{
    struct event_args args;
    const struct probe_desc *desc;

    if (parse_events_args(argc, argv, &args) < 0) {
        lp_print_usage("lightprobe");
        return 2;
    }

    if (args.func != NULL) {
        desc = lp_probe_manager_find(args.pid, args.func);
    } else {
        desc = find_first_probe_by_pid(args.pid);
    }

    if (desc == NULL) {
        fprintf(stderr, "events failed: probe not found\n");
        return 1;
    }

    if (read_and_print_events(desc, &args) < 0) {
        fprintf(stderr, "events failed: %s\n", strerror(errno));
        return 1;
    }

    return 0;
}
