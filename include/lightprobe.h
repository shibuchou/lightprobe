#ifndef LIGHTPROBE_LIGHTPROBE_H
#define LIGHTPROBE_LIGHTPROBE_H

#include "event.h"
#include "probe_types.h"

#ifdef __cplusplus
extern "C" {
#endif

int lp_probe_attach(const struct lp_probe_spec *spec, struct probe_desc *out_desc);
int lp_probe_detach(struct probe_desc *desc);
int lp_probe_enable(struct probe_desc *desc);
int lp_probe_disable(struct probe_desc *desc);

int lp_probe_manager_init(void);
struct probe_desc *lp_probe_manager_alloc(const struct lp_probe_spec *spec);
struct probe_desc *lp_probe_manager_find(pid_t pid, const char *func_name);
void lp_probe_manager_remove(struct probe_desc *desc);

#ifdef __cplusplus
}
#endif

#endif

