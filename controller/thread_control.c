

#include "controller_internal.h"

#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>



int stop_all_threads(pid_t pid)
{
    DIR *dir = NULL;
    struct dirent *entry;

    char task_path[64];

    if (pid <= 0) {
        errno = EINVAL;
        return -1;
    }

    snprintf(task_path, sizeof(task_path),
             "/proc/%d/task", pid);

    dir = opendir(task_path);
    if (!dir) {
        return -1;
    }

    while ((entry = readdir(dir)) != NULL) {

        pid_t tid;
        int status;

        
        if (entry->d_name[0] == '.') {
            continue;
        }

        tid = (pid_t)atoi(entry->d_name);

        if (tid <= 0) {
            continue;
        }

        if(pid==tid){
            continue;
        }

        
        if (ptrace(PTRACE_ATTACH, tid, NULL, NULL) == -1) {

            
            if (errno == ESRCH) {
                continue;
            }

            closedir(dir);
            return -1;
        }

        
        if (waitpid(tid, &status, __WALL) == -1) {

            ptrace(PTRACE_DETACH, tid, NULL, NULL);

            closedir(dir);
            return -1;
        }

        
        if (!WIFSTOPPED(status)) {

            ptrace(PTRACE_DETACH, tid, NULL, NULL);

            closedir(dir);

            errno = EBUSY;
            return -1;
        }
    }

    closedir(dir);

    return 0;
}



int resume_all_threads(pid_t pid)
{
    DIR *dir = NULL;
    struct dirent *entry;

    char task_path[64];

    if (pid <= 0) {
        errno = EINVAL;
        return -1;
    }

    snprintf(task_path, sizeof(task_path),
             "/proc/%d/task", pid);

    dir = opendir(task_path);
    if (!dir) {
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
        
        if(pid==tid){
            continue; 	
        }

        
        if (ptrace(PTRACE_DETACH, tid, NULL, 0) == -1) {

            
            if (errno == ESRCH) {
                continue;
            }

            closedir(dir);
            return -1;
        }
    }

    closedir(dir);

    return 0;
}
