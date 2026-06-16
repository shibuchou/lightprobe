#include <stdio.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

int main(void)
{
    unsigned long counter = 0;

    setvbuf(stdout, NULL, _IOLBF, 0);
    printf("lightprobe_getpid_loop pid=%d\n", getpid());

    while (1) {
        (void)getpid();
        counter++;
        if ((counter % 1000000UL) == 0) {
            printf("heartbeat counter=%lu\n", counter);
        }
        struct timespec ts = {0, 1000 * 1000};
        nanosleep(&ts, NULL);
    }
}