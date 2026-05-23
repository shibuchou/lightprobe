#include "lightprobe.h"

#include "arch_x86_64.h"
#include "controller.h"
#include "injector.h"
#include "runtime.h"

#include <errno.h>
#include <sys/mman.h>
#include <stdio.h>
#include <string.h>

#define LP_STATE_FILE "/tmp/lightprobe_state.bin"
#define LP_STATE_MAGIC 0x4C505442U
#define LP_STATE_VERSION 1U

struct lp_probe_state_file {
    uint32_t magic;
    uint32_t version;
    struct probe_desc probes[LP_MAX_PROBES];
};

static struct probe_desc g_probe_table[LP_MAX_PROBES];

static int lp_update_remote_enabled(struct probe_desc *desc, int enabled)
{
    uint32_t remote_enabled = enabled ? 1U : 0U;

    if (desc->config_addr == 0) {
        errno = ENOENT;
        return -1;
    }

    return lp_remote_write(desc->pid,
                           lp_runtime_config_enabled_addr(desc),
                           &remote_enabled,
                           sizeof(remote_enabled));
}

static void lp_cleanup_remote_runtime_best_effort(struct probe_desc *desc)
{
    if (desc->runtime_base_addr == 0 || desc->runtime_size == 0) {
        return;
    }

    if (lp_remote_munmap(desc->pid, desc->runtime_base_addr,
                         (size_t)desc->runtime_size) < 0) {
        /*
         * Remote munmap is optional at this stage. Detach must still restore
         * the original instruction even when remote runtime cleanup is not
         * implemented yet.
         */
        return;
    }
}

static int lp_install_remote_runtime(pid_t pid, struct probe_desc *desc)
{
    struct lp_remote_runtime_layout sizing;
    struct lp_remote_runtime_layout layout;
    struct lp_runtime_config config;
    struct lp_event_buffer event_buffer;
    struct lp_shadow_stack shadow_stack;
    unsigned char trampoline[LP_TRAMPOLINE_SIZE];
    unsigned char entry_stub[LP_REMOTE_STUB_SIZE];
    unsigned char ret_stub[LP_REMOTE_STUB_SIZE];
    size_t trampoline_len = 0;
    size_t entry_stub_len = 0;
    size_t ret_stub_len = 0;
    uint64_t remote_base = 0;

    if (lp_remote_runtime_layout_init(&sizing, 0) < 0) {
        return -1;
    }

    if (lp_remote_mmap(pid, sizing.size, PROT_READ | PROT_WRITE | PROT_EXEC,
                       &remote_base) < 0) {
        return -1;
    }

    if (lp_remote_runtime_layout_init(&layout, remote_base) < 0) {
        return -1;
    }
    lp_probe_desc_set_runtime_layout(desc, &layout);

    if (lp_runtime_config_init(&config, desc) < 0) {
        return -1;
    }
    lp_event_buffer_init(&event_buffer);
    lp_shadow_stack_init(&shadow_stack);

    if (lp_x86_64_build_trampoline(desc,
                                   desc->target_addr + (uint64_t)desc->original_len,
                                   trampoline,
                                   sizeof(trampoline),
                                   &trampoline_len) < 0) {
        return -1;
    }
    if (lp_x86_64_build_entry_stub(desc, entry_stub, sizeof(entry_stub),
                                   &entry_stub_len) < 0) {
        return -1;
    }
    if (lp_x86_64_build_ret_stub(desc, ret_stub, sizeof(ret_stub), &ret_stub_len) < 0) {
        return -1;
    }

    if (lp_remote_write(pid, desc->config_addr, &config, sizeof(config)) < 0) {
        return -1;
    }
    if (lp_remote_write(pid, desc->event_buffer_addr, &event_buffer,
                        sizeof(event_buffer)) < 0) {
        return -1;
    }
    if (lp_remote_write(pid, desc->shadow_stack_addr, &shadow_stack,
                        sizeof(shadow_stack)) < 0) {
        return -1;
    }
    if (lp_remote_write(pid, desc->trampoline_addr, trampoline, trampoline_len) < 0) {
        return -1;
    }
    if (lp_remote_write(pid, desc->entry_stub_addr, entry_stub, entry_stub_len) < 0) {
        return -1;
    }
    if (lp_remote_write(pid, desc->ret_stub_addr, ret_stub, ret_stub_len) < 0) {
        return -1;
    }

    return 0;
}

int lp_probe_manager_init(void)
{
    memset(g_probe_table, 0, sizeof(g_probe_table));
    for (int i = 0; i < LP_MAX_PROBES; i++) {
        g_probe_table[i].probe_id = i;
    }
    return 0;
}

int lp_probe_manager_load(void)
{
    struct lp_probe_state_file state;
    FILE *fp = fopen(LP_STATE_FILE, "rb");
    if (fp == NULL) {
        if (errno == ENOENT) {
            return lp_probe_manager_init();
        }
        return -1;
    }

    size_t nread = fread(&state, sizeof(state), 1, fp);
    if (fclose(fp) != 0) {
        return -1;
    }
    if (nread != 1) {
        errno = EIO;
        return -1;
    }
    if (state.magic != LP_STATE_MAGIC || state.version != LP_STATE_VERSION) {
        errno = EINVAL;
        return -1;
    }

    memcpy(g_probe_table, state.probes, sizeof(g_probe_table));
    return 0;
}

int lp_probe_manager_save(void)
{
    struct lp_probe_state_file state = {
        .magic = LP_STATE_MAGIC,
        .version = LP_STATE_VERSION,
    };
    memcpy(state.probes, g_probe_table, sizeof(g_probe_table));

    FILE *fp = fopen(LP_STATE_FILE, "wb");
    if (fp == NULL) {
        return -1;
    }

    size_t nwritten = fwrite(&state, sizeof(state), 1, fp);
    if (fclose(fp) != 0) {
        return -1;
    }
    if (nwritten != 1) {
        errno = EIO;
        return -1;
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

const struct probe_desc *lp_probe_manager_at(int index)
{
    if (index < 0 || index >= LP_MAX_PROBES) {
        errno = EINVAL;
        return NULL;
    }
    return &g_probe_table[index];
}

void lp_probe_manager_remove(struct probe_desc *desc)
{
    if (desc != NULL) {
        memset(desc, 0, sizeof(*desc));
    }
}

int lp_probe_attach(const struct lp_probe_spec *spec, struct probe_desc *out_desc)
{
    if (lp_probe_manager_find(spec->pid, spec->func_name) != NULL) {
        errno = EEXIST;
        return -1;
    }

    struct probe_desc *desc = lp_probe_manager_alloc(spec);
    if (desc == NULL) {
        return -1;
    }

    if (lp_resolve_symbol(spec->pid, spec->lib_name, spec->func_name,
                          &desc->target_addr) < 0) {
        lp_probe_manager_remove(desc);
        return -1;
    }

    if (lp_stop_all_threads(spec->pid) < 0) {
        lp_probe_manager_remove(desc);
        return -1;
    }

    if (lp_x86_64_read_original_code(spec->pid, desc, LP_X86_64_JMP_REL32_SIZE) < 0) {
        int saved_errno = errno;
        (void)lp_resume_all_threads(spec->pid);
        lp_probe_manager_remove(desc);
        errno = saved_errno;
        return -1;
    }

    if (lp_install_remote_runtime(spec->pid, desc) < 0) {
        int saved_errno = errno;
        (void)lp_resume_all_threads(spec->pid);
        lp_probe_manager_remove(desc);
        errno = saved_errno;
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

    if (lp_probe_manager_save() < 0) {
        return -1;
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

    lp_cleanup_remote_runtime_best_effort(desc);
    (void)lp_resume_all_threads(desc->pid);
    lp_probe_manager_remove(desc);
    if (lp_probe_manager_save() < 0) {
        return -1;
    }
    return 0;
}

int lp_probe_enable(struct probe_desc *desc)
{
    if (desc == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (lp_update_remote_enabled(desc, 1) < 0) {
        return -1;
    }
    desc->enabled = 1;
    if (lp_probe_manager_save() < 0) {
        return -1;
    }
    return 0;
}

int lp_probe_disable(struct probe_desc *desc)
{
    if (desc == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (lp_update_remote_enabled(desc, 0) < 0) {
        return -1;
    }
    desc->enabled = 0;
    if (lp_probe_manager_save() < 0) {
        return -1;
    }
    return 0;
}
