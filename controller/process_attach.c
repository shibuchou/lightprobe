
#include "controller_internal.h"
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <unistd.h>



int process_attach(pid_t pid)
{
    int status = 0;

    if (pid <= 0) {
        errno = EINVAL;
        return -1;
    }

    
    if (ptrace(PTRACE_ATTACH, pid, NULL, NULL) == -1) {
        return -1;
    }

    
    if (waitpid(pid, &status, __WALL) == -1) {

        ptrace(PTRACE_DETACH, pid, NULL, NULL);

        return -1;
    }

    
    if (!WIFSTOPPED(status)) {

        ptrace(PTRACE_DETACH, pid, NULL, NULL);

        errno = EBUSY;
        return -1;
    }

    return 0;
}



int process_detach(pid_t pid)
{
    if (pid <= 0) {
        errno = EINVAL;
        return -1;
    }

    if (ptrace(PTRACE_DETACH, pid, NULL, 0) == -1) {
        return -1;
    }

    return 0;
}