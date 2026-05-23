#include "injector.h"
#include "runtime.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

static void init_desc(struct probe_desc *desc, int has_retprobe)
{
    struct lp_remote_runtime_layout layout;

    memset(desc, 0, sizeof(*desc));
    if (lp_remote_runtime_layout_init(&layout, 0x700000000000ULL) < 0) {
        perror("lp_remote_runtime_layout_init");
        return;
    }

    lp_probe_desc_set_runtime_layout(desc, &layout);
    desc->probe_id = 7;
    desc->pid = 1234;
    desc->target_addr = 0x401000;
    desc->enabled = 1;
    desc->has_retprobe = has_retprobe;
}

static int expect_stub_fits(const char *name, size_t written)
{
    if (written == 0 || written > LP_REMOTE_STUB_SIZE) {
        fprintf(stderr, "%s wrote invalid length: %lu\n", name, (unsigned long)written);
        return -1;
    }
    return 0;
}

static int test_entry_stub_without_retprobe(void)
{
    struct probe_desc desc;
    unsigned char buf[LP_REMOTE_STUB_SIZE];
    size_t written = 0;

    init_desc(&desc, 0);
    if (lp_x86_64_build_entry_stub(&desc, buf, sizeof(buf), &written) < 0) {
        perror("lp_x86_64_build_entry_stub non-retprobe");
        return -1;
    }

    return expect_stub_fits("entry stub without retprobe", written);
}

static int test_entry_stub_with_retprobe(void)
{
    struct probe_desc desc;
    unsigned char buf[LP_REMOTE_STUB_SIZE];
    size_t written = 0;

    init_desc(&desc, 1);
    if (lp_x86_64_build_entry_stub(&desc, buf, sizeof(buf), &written) < 0) {
        perror("lp_x86_64_build_entry_stub retprobe");
        return -1;
    }

    return expect_stub_fits("entry stub with retprobe", written);
}

static int test_ret_stub_with_shadow_stack(void)
{
    struct probe_desc desc;
    unsigned char buf[LP_REMOTE_STUB_SIZE];
    size_t written = 0;

    init_desc(&desc, 1);
    if (lp_x86_64_build_ret_stub(&desc, buf, sizeof(buf), &written) < 0) {
        perror("lp_x86_64_build_ret_stub");
        return -1;
    }

    return expect_stub_fits("ret stub", written);
}

static int test_ret_stub_without_shadow_stack(void)
{
    struct probe_desc desc;
    unsigned char buf[LP_REMOTE_STUB_SIZE];
    size_t written = 123;

    init_desc(&desc, 1);
    desc.shadow_stack_addr = 0;
    errno = 0;
    if (lp_x86_64_build_ret_stub(&desc, buf, sizeof(buf), &written) == 0) {
        fputs("lp_x86_64_build_ret_stub unexpectedly succeeded without shadow stack\n", stderr);
        return -1;
    }
    if (errno != EINVAL) {
        fprintf(stderr, "expected errno=EINVAL actual=%d\n", errno);
        return -1;
    }

    return 0;
}

int main(void)
{
    if (test_entry_stub_without_retprobe() < 0 ||
        test_entry_stub_with_retprobe() < 0 ||
        test_ret_stub_with_shadow_stack() < 0 ||
        test_ret_stub_without_shadow_stack() < 0) {
        return 1;
    }

    puts("stub builder tests passed");
    return 0;
}
