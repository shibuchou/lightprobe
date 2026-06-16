#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

extern int f01(int x);
extern int f02(int x);
extern int f03(int x);
extern int f04(int x);
extern int f05(int x);
extern int f06(int x);
extern int f07(int x);
extern int f08(int x);
extern int f09(int x);
extern int f10(int x);
extern int f11(int x);
extern int f12(int x);
extern int f13(int x);
extern int f14(int x);
extern int f15(int x);
extern int f16(int x);

typedef int (*func_t)(int);

static const func_t funcs[] = {
    f01, f02, f03, f04, f05, f06, f07, f08,
    f09, f10, f11, f12, f13, f14, f15, f16,
};

#define NFUNCS 16

int main(void)
{
    int results[NFUNCS];

    setvbuf(stdout, NULL, _IOLBF, 0);
    printf("lightprobe_multi_func pid=%d nfuncs=%d\n", getpid(), NFUNCS);

    int round = 0;
    while (1) {
        for (int i = 0; i < NFUNCS; i++) {
            results[i] = funcs[i](0);
        }
        round++;
        if ((round % 10000) == 0) {
            printf("round=%d ok\n", round);
            for (int i = 0; i < NFUNCS; i++) {
                int expected = i + 1;
                if (results[i] != expected) {
                    fprintf(stderr, "f%02d(0) = %d, expected %d\n",
                            i + 1, results[i], expected);
                    return 1;
                }
            }
        }
        struct timespec ts = {0, 500 * 1000};
        nanosleep(&ts, NULL);
    }

    return 0;
}
