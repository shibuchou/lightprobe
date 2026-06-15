#include "controller_internal.h"

#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static int attach_one_thread(pid_t tid)
{
    int status = 0;

    if (ptrace(PTRACE_ATTACH, tid, NULL, NULL) < 0) {
        return -1;
    }

    if (waitpid(tid, &status, __WALL) < 0) {
        int saved_errno = errno;
        (void)ptrace(PTRACE_DETACH, tid, NULL, NULL);
        errno = saved_errno;
        return -1;
    }

    if (!WIFSTOPPED(status)) {
        (void)ptrace(PTRACE_DETACH, tid, NULL, NULL);
        errno = EBUSY;
        return -1;
    }

    return 0;
}

static void detach_recorded_threads(const pid_t *tids, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        (void)ptrace(PTRACE_DETACH, tids[i], NULL, NULL);
    }
}

int stop_all_threads(pid_t pid)
{
    DIR *dir = NULL;
    struct dirent *entry;
    pid_t attached_tids[4096];
    size_t attached_count = 0;
    char task_path[64];

    if (pid <= 0) {
        errno = EINVAL;
        return -1;
    }

    snprintf(task_path, sizeof(task_path), "/proc/%d/task", pid);
    dir = opendir(task_path);
    if (dir == NULL) {
        return -1;
    }

    while ((entry = readdir(dir)) != NULL) {
        pid_t tid;

        if (entry->d_name[0] == '.') {
            continue;
        }

        tid = (pid_t)atoi(entry->d_name);
        if (tid <= 0) {
            continue;
        }
        if (attached_count >= sizeof(attached_tids) / sizeof(attached_tids[0])) {
            int saved_errno = ENOSPC;
            closedir(dir);
            detach_recorded_threads(attached_tids, attached_count);
            errno = saved_errno;
            return -1;
        }

        if (attach_one_thread(tid) < 0) {
            int saved_errno = errno;
            if (saved_errno == ESRCH) {
                continue;
            }
            closedir(dir);
            detach_recorded_threads(attached_tids, attached_count);
            errno = saved_errno;
            return -1;
        }

        attached_tids[attached_count++] = tid;
    }

    closedir(dir);
    return 0;
}

int resume_all_threads(pid_t pid)
{
    DIR *dir = NULL;
    struct dirent *entry;
    int first_errno = 0;
    char task_path[64];

    if (pid <= 0) {
        errno = EINVAL;
        return -1;
    }

    snprintf(task_path, sizeof(task_path), "/proc/%d/task", pid);
    dir = opendir(task_path);
    if (dir == NULL) {
        return -1;
    }

    while ((entry = readdir(dir)) != NULL) {
        pid_t tid;

        if (entry->d_name[0] == '.') {
            continue;
        }

        tid = (pid_t)atoi(entry->d_name);
        if (tid <= 0) {
            continue;
        }

        if (ptrace(PTRACE_DETACH, tid, NULL, NULL) < 0 &&
            errno != ESRCH) {
            if (first_errno == 0) {
                first_errno = errno;
            }
        }
    }

    closedir(dir);
    if (first_errno != 0) {
        errno = first_errno;
        return -1;
    }
    return 0;
}
