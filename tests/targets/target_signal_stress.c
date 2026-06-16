#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#define WORKER_COUNT      4
#define DEFAULT_DURATION  60
#define SIGNAL_INTERVAL_NS (2L * 1000L * 1000L)

static atomic_ulong signal_received = 0;
static atomic_ulong signals_sent   = 0;
static atomic_int   running        = 1;

static void sigusr1_handler(int sig)
{
    (void)sig;
    atomic_fetch_add(&signal_received, 1);
}

static void *worker_main(void *arg)
{
    long worker_id = (long)arg;
    unsigned long local_counter = 0;

    while (atomic_load(&running)) {
        size_t size = 64 + (local_counter % 128);
        char *ptr = (char *)malloc(size);
        if (ptr != NULL) {
            free(ptr);
        }
        (void)getpid();
        local_counter++;
        if ((local_counter % 100000UL) == 0) {
            printf("worker=%ld local=%lu sig_rcvd=%lu sig_sent=%lu\n",
                   worker_id, local_counter,
                   atomic_load(&signal_received),
                   atomic_load(&signals_sent));
        }
        struct timespec ts = {0, 1000 * 1000};
        nanosleep(&ts, NULL);
    }

    printf("worker=%ld final_local=%lu\n", worker_id, local_counter);
    return NULL;
}

int main(int argc, char **argv)
{
    int duration = DEFAULT_DURATION;
    long interval_ns = SIGNAL_INTERVAL_NS;

    if (argc >= 2) {
        duration = atoi(argv[1]);
        if (duration <= 0) {
            duration = DEFAULT_DURATION;
        }
    }
    if (argc >= 3) {
        interval_ns = atol(argv[2]);
        if (interval_ns <= 0) {
            interval_ns = SIGNAL_INTERVAL_NS;
        }
    }

    pthread_t workers[WORKER_COUNT];
    struct sigaction sa;
    sa.sa_handler = sigusr1_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGUSR1, &sa, NULL);

    setvbuf(stdout, NULL, _IOLBF, 0);
    printf("lightprobe_signal_stress pid=%d duration=%ds workers=%d interval_ns=%ld\n",
           getpid(), duration, WORKER_COUNT, interval_ns);

    for (long index = 0; index < WORKER_COUNT; index++) {
        if (pthread_create(&workers[index], NULL, worker_main, (void *)index) != 0) {
            perror("pthread_create");
            return 1;
        }
    }

    time_t start = time(NULL);
    while (time(NULL) - start < duration) {
        for (int i = 0; i < WORKER_COUNT; i++) {
            pthread_kill(workers[i], SIGUSR1);
            atomic_fetch_add(&signals_sent, 1);
        }
        struct timespec ts = {0, interval_ns};
        nanosleep(&ts, NULL);
    }

    atomic_store(&running, 0);

    for (int i = 0; i < WORKER_COUNT; i++) {
        pthread_join(workers[i], NULL);
    }

    printf("signal_stress done pid=%d sent=%lu received=%lu duration=%d\n",
           getpid(),
           atomic_load(&signals_sent),
           atomic_load(&signal_received),
           duration);
    return 0;
}
