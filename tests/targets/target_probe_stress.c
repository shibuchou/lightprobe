#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_THREADS 8

static atomic_ulong total_calls = 0;
static int worker_count = DEFAULT_THREADS;
static int sleep_ns = 100000;
static size_t (*volatile strlen_call)(const char *) = strlen;

static void *worker_main(void *arg)
{
    long worker_id = (long)arg;
    unsigned long local = 0;

    while (1) {
        char text[64];
        int len = snprintf(text, sizeof(text), "worker-%ld-%lu", worker_id, local);
        if (len > 0) {
            volatile size_t text_len = strlen_call(text);
            char *ptr = (char *)malloc(text_len + 1);
            if (ptr != NULL) {
                memcpy(ptr, text, text_len + 1);
                free(ptr);
            }
        }
        local++;
        if ((local % 10000UL) == 0) {
            unsigned long total = atomic_fetch_add(&total_calls, 10000UL) + 10000UL;
            printf("worker=%ld local=%lu total=%lu\n", worker_id, local, total);
        }
        if (sleep_ns > 0) {
            struct timespec ts = {0, sleep_ns};
            nanosleep(&ts, NULL);
        }
    }

    return NULL;
}

int main(int argc, char **argv)
{
    if (argc >= 2) {
        worker_count = atoi(argv[1]);
        if (worker_count <= 0 || worker_count > 64) {
            worker_count = DEFAULT_THREADS;
        }
    }
    if (argc >= 3) {
        sleep_ns = atoi(argv[2]);
        if (sleep_ns < 0) {
            sleep_ns = 0;
        }
    }

    pthread_t *workers = calloc((size_t)worker_count, sizeof(*workers));
    if (workers == NULL) {
        perror("calloc");
        return 1;
    }

    setvbuf(stdout, NULL, _IOLBF, 0);
    printf("lightprobe_stress pid=%d workers=%d sleep_ns=%d strlen_addr=0x%lx\n",
           getpid(),
           worker_count,
           sleep_ns,
           (unsigned long)(uintptr_t)strlen_call);

    for (long index = 0; index < worker_count; index++) {
        if (pthread_create(&workers[index], NULL, worker_main, (void *)index) != 0) {
            perror("pthread_create");
            return 1;
        }
    }

    worker_main((void *)(long)worker_count);
    return 0;
}
