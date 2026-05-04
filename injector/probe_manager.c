#include "lightprobe.h"

#include "arch_x86_64.h"
#include "controller.h"
#include "injector.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

static struct probe_desc g_probe_table[LP_MAX_PROBES];

int lp_probe_manager_init(void)
{
    memset(g_probe_table, 0, sizeof(g_probe_table));
    for (int i = 0; i < LP_MAX_PROBES; i++) {
        g_probe_table[i].probe_id = i;
    }
    return 0;
}

struct probe_desc *lp_probe_manager_alloc(const struct lp_probe_spec *spec)
{
    for (int i = 0; i < LP_MAX_PROBES; i++) {
        if (g_probe_table[i].pid == 0) {
            struct probe_desc *desc = &g_probe_table[i];
            memset(desc, 0, sizeof(*desc));
            desc->probe_id = i;
            desc->pid = spec->pid;
            desc->has_retprobe = spec->has_retprobe;
            desc->enabled = 1;
            snprintf(desc->lib_name, sizeof(desc->lib_name), "%s", spec->lib_name);
            snprintf(desc->func_name, sizeof(desc->func_name), "%s", spec->func_name);
            return desc;
        }
    }

    errno = ENOSPC;
    return NULL;
}

struct probe_desc *lp_probe_manager_find(pid_t pid, const char *func_name)
{
    for (int i = 0; i < LP_MAX_PROBES; i++) {
        if (g_probe_table[i].pid == pid &&
            strncmp(g_probe_table[i].func_name, func_name, LP_MAX_NAME) == 0) {
            return &g_probe_table[i];
        }
    }
    return NULL;
}

void lp_probe_manager_remove(struct probe_desc *desc)
{
    if (desc != NULL) {
        memset(desc, 0, sizeof(*desc));
    }
}

int lp_probe_attach(const struct lp_probe_spec *spec, struct probe_desc *out_desc)
{
    struct probe_desc *desc = lp_probe_manager_alloc(spec);
    if (desc == NULL) {
        return -1;
    }

    if (lp_resolve_symbol(spec->pid, spec->lib_name, spec->func_name,
                          &desc->target_addr) < 0) {
        lp_probe_manager_remove(desc);
        return -1;
    }

    desc->original_len = LP_X86_64_JMP_REL32_SIZE;

    if (lp_stop_all_threads(spec->pid) < 0) {
        lp_probe_manager_remove(desc);
        return -1;
    }

    if (lp_x86_64_patch_entry(spec->pid, desc) < 0) {
        int saved_errno = errno;
        (void)lp_resume_all_threads(spec->pid);
        lp_probe_manager_remove(desc);
        errno = saved_errno;
        return -1;
    }

    (void)lp_resume_all_threads(spec->pid);

    if (out_desc != NULL) {
        *out_desc = *desc;
    }
    return 0;
}

int lp_probe_detach(struct probe_desc *desc)
{
    if (desc == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (lp_stop_all_threads(desc->pid) < 0) {
        return -1;
    }

    if (lp_x86_64_restore_entry(desc->pid, desc) < 0) {
        int saved_errno = errno;
        (void)lp_resume_all_threads(desc->pid);
        errno = saved_errno;
        return -1;
    }

    (void)lp_resume_all_threads(desc->pid);
    lp_probe_manager_remove(desc);
    return 0;
}

int lp_probe_enable(struct probe_desc *desc)
{
    if (desc == NULL) {
        errno = EINVAL;
        return -1;
    }
    desc->enabled = 1;
    return 0;
}

int lp_probe_disable(struct probe_desc *desc)
{
    if (desc == NULL) {
        errno = EINVAL;
        return -1;
    }
    desc->enabled = 0;
    return 0;
}
