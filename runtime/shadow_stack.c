#include "runtime.h"

#include <string.h>

void lp_shadow_stack_init(struct lp_shadow_stack *stack)
{
    memset(stack, 0, sizeof(*stack));
}
