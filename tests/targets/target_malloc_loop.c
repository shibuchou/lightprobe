#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

int main(void)
{
    unsigned long counter = 0;

    setvbuf(stdout, NULL, _IOLBF, 0);
    printf("lightprobe_malloc_loop pid=%d\n", getpid());

    while (1) {
        size_t size = 32 + (counter % 96);
        char *ptr = (char *)malloc(size);
        if (ptr != NULL) {
            memset(ptr, (int)(counter & 0xff), size);
            free(ptr);
        }
        counter++;
        if ((counter % 200000UL) == 0) {
            printf("heartbeat counter=%lu\n", counter);
        }
        struct timespec ts = {0, 1000 * 1000};
        nanosleep(&ts, NULL);
    }
}