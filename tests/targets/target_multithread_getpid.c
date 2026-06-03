#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define WORKER_COUNT 4

static atomic_ulong global_counter = 0;

static void *worker_main(void *arg)
{
    long worker_id = (long)arg;
    unsigned long local_counter = 0;

    while (1) {
        (void)getpid();
        local_counter++;
        if ((local_counter % 200000UL) == 0) {
            unsigned long total = atomic_fetch_add(&global_counter, 200000UL) + 200000UL;
            printf("worker=%ld local=%lu total=%lu\n", worker_id, local_counter, total);
        }
        struct timespec ts = {0, 1000 * 1000};
        nanosleep(&ts, NULL);
    }

    return NULL;
}

int main(void)
{
    pthread_t workers[WORKER_COUNT];

    setvbuf(stdout, NULL, _IOLBF, 0);
    printf("lightprobe_multithread_getpid pid=%d workers=%d\n", getpid(), WORKER_COUNT);

    for (long index = 0; index < WORKER_COUNT; index++) {
        if (pthread_create(&workers[index], NULL, worker_main, (void *)index) != 0) {
            perror("pthread_create");
            return 1;
        }
    }

    worker_main((void *)WORKER_COUNT);
    return 0;
}