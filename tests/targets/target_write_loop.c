#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

int main(void)
{
    unsigned long counter = 0;
    const char payload[] = ".";

    setvbuf(stdout, NULL, _IOLBF, 0);
    printf("lightprobe_write_loop pid=%d\n", getpid());

    while (1) {
        ssize_t written = write(STDOUT_FILENO, payload, strlen(payload));
        if (written < 0) {
            perror("write");
            return 1;
        }
        counter++;
        if ((counter % 80UL) == 0) {
            printf(" heartbeat counter=%lu\n", counter);
        }
        struct timespec ts = {0, 10 * 1000 * 1000};
        nanosleep(&ts, NULL);
    }
}
