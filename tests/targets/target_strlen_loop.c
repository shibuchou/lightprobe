#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static const char *samples[] = {
    "lightprobe",
    "entry-probe",
    "return-probe",
    "strlen-target",
};

static size_t (*volatile strlen_call)(const char *) = strlen;
static size_t (*volatile *strlen_slot)(const char *) = &strlen_call;

int main(void)
{
    unsigned long counter = 0;
    volatile size_t sink = 0;

    setvbuf(stdout, NULL, _IOLBF, 0);
    printf("lightprobe_strlen_loop pid=%d strlen_slot=0x%lx strlen_addr=0x%lx\n",
           getpid(),
           (unsigned long)(uintptr_t)strlen_slot,
           (unsigned long)(uintptr_t)strlen_call);

    while (1) {
        const char *text = samples[counter % (sizeof(samples) / sizeof(samples[0]))];
        sink += strlen_call(text);
        counter++;
        if ((counter % 200000UL) == 0) {
            printf("heartbeat counter=%lu sink=%lu\n", counter, (unsigned long)sink);
        }
        struct timespec ts = {0, 1000 * 1000};
        nanosleep(&ts, NULL);
    }
}
