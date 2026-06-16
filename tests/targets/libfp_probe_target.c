#include <math.h>

#define EXPORT __attribute__((visibility("default")))

EXPORT
__attribute__((noinline))
double fp_mix(double a, double b)
{
    asm volatile(
        "nop\n\t""nop\n\t""nop\n\t""nop\n\t"
        "nop\n\t""nop\n\t""nop\n\t""nop\n\t"
        ::: "memory"
    );
    return sin(a) + cos(b) + a * b;
}
