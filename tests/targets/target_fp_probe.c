#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>

extern double fp_mix(double a, double b);

static double (*volatile fp_mix_ptr)(double, double) = fp_mix;

int main(void)
{
    unsigned long counter = 0;

    setvbuf(stdout, NULL, _IOLBF, 0);
    printf("target_fp_probe pid=%d fp_mix=0x%lx\n",
           getpid(), (unsigned long)(uintptr_t)fp_mix);

    while (1) {
        double result = fp_mix_ptr(1.0, 2.0);
        counter++;
        if ((counter % 5UL) == 0) {
            printf("result=%.12f\n", result);
        }
        struct timespec ts = {0, 500L * 1000L * 1000L};
        nanosleep(&ts, NULL);
    }
}
