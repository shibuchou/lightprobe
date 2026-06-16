#define _GNU_SOURCE
#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t g_ready = 0;

static void sigusr1_handler(int sig)
{
    (void)sig;
    g_ready = 1;
}

static void wait_for_signal(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigusr1_handler;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR1, &sa, NULL);

    printf("lightprobe_getpid_bench pid=%d waiting...\n", getpid());
    fflush(stdout);

    while (!g_ready) {
        pause();
    }
}

static void warn(const char *msg)
{
    fprintf(stderr, "WARNING: %s\n", msg);
}

static double calc_stddev(const double *values, int n, double mean)
{
    if (n <= 1) return 0.0;
    double sum_sq = 0.0;
    for (int i = 0; i < n; i++) {
        double diff = values[i] - mean;
        sum_sq += diff * diff;
    }
    return sqrt(sum_sq / (double)(n - 1));
}

int main(int argc, char *argv[])
{
    const char *label = "baseline";
    long iters = 1000000L;
    int rounds = 10;
    int warmup = 2;
    int do_wait = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--label") == 0 && i + 1 < argc) {
            label = argv[++i];
        } else if (strcmp(argv[i], "--iters") == 0 && i + 1 < argc) {
            iters = atol(argv[++i]);
            if (iters <= 0) {
                fprintf(stderr, "iters must be positive, got %ld\n", iters);
                return 1;
            }
        } else if (strcmp(argv[i], "--rounds") == 0 && i + 1 < argc) {
            rounds = atoi(argv[++i]);
            if (rounds < 1) {
                fprintf(stderr, "rounds must be >= 1, got %d\n", rounds);
                return 1;
            }
        } else if (strcmp(argv[i], "--warmup") == 0 && i + 1 < argc) {
            warmup = atoi(argv[++i]);
            if (warmup < 0) {
                fprintf(stderr, "warmup must be >= 0, got %d\n", warmup);
                return 1;
            }
        } else if (strcmp(argv[i], "--wait") == 0) {
            do_wait = 1;
        } else {
            fprintf(stderr, "unknown argument: %s\n", argv[i]);
            return 1;
        }
    }

    setvbuf(stdout, NULL, _IOLBF, 0);

    if (do_wait) {
        wait_for_signal();
    } else {
        printf("lightprobe_getpid_bench pid=%d\n", getpid());
    }

    for (int w = 0; w < warmup; w++) {
        for (long j = 0; j < iters; j++) {
            (void)getpid();
        }
    }

    double *per_call_ns = (double *)calloc((size_t)rounds, sizeof(double));
    if (per_call_ns == NULL) {
        fprintf(stderr, "calloc failed\n");
        return 1;
    }

    double avg_ns_total = 0.0;
    double min_ns_per_call = 1e18;
    double max_ns_per_call = 0.0;

    for (int r = 0; r < rounds; r++) {
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);

        for (long j = 0; j < iters; j++) {
            (void)getpid();
        }

        clock_gettime(CLOCK_MONOTONIC, &t1);

        double delta_s = (double)(t1.tv_sec - t0.tv_sec) +
                         (double)(t1.tv_nsec - t0.tv_nsec) / 1e9;
        double total_ns = delta_s * 1e9;
        double pc = total_ns / (double)iters;

        avg_ns_total += total_ns;
        per_call_ns[r] = pc;

        if (pc < min_ns_per_call) min_ns_per_call = pc;
        if (pc > max_ns_per_call) max_ns_per_call = pc;
    }

    avg_ns_total /= (double)rounds;

    double avg_ns_per_call = 0.0;
    for (int r = 0; r < rounds; r++) {
        avg_ns_per_call += per_call_ns[r];
    }
    avg_ns_per_call /= (double)rounds;

    double stddev_ns_per_call = calc_stddev(per_call_ns, rounds, avg_ns_per_call);

    printf("{"
           "\"label\":\"%s\","
           "\"iters\":%ld,"
           "\"rounds\":%d,"
           "\"warmup\":%d,"
           "\"avg_ns_total\":%.2f,"
           "\"avg_ns_per_call\":%.2f,"
           "\"stddev_ns_per_call\":%.2f,"
           "\"min_ns_per_call\":%.2f,"
           "\"max_ns_per_call\":%.2f"
           "}\n",
           label, iters, rounds, warmup,
           avg_ns_total, avg_ns_per_call, stddev_ns_per_call,
           min_ns_per_call, max_ns_per_call);
    fflush(stdout);

    if (min_ns_per_call > 1e17) {
        warn("no valid rounds recorded; min/max may be unreliable");
    }

    free(per_call_ns);
    return 0;
}
