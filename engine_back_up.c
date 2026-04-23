#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sched.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <time.h>
#include <signal.h>

#define STACK_SIZE (1024 * 1024)
#define MAX_CONTAINERS 16

typedef enum {
    STATE_STARTING,
    STATE_RUNNING,
    STATE_STOPPED,
    STATE_KILLED
} ContainerState;

typedef struct {
    char           id[64];
    pid_t          host_pid;
    time_t         start_time;
    ContainerState state;
    long           soft_limit_mb;
    long           hard_limit_mb;
    char           log_path[256];
    int            exit_status;
} ContainerMeta;

ContainerMeta containers[MAX_CONTAINERS];
int num_containers = 0;

static int container_main(void *arg) {
    char *rootfs = (char *)arg;

    if (sethostname("container", 9) != 0) {
        perror("sethostname");
        return 1;
    }

    if (chroot(rootfs) != 0) {
        perror("chroot");
        return 1;
    }
    if (chdir("/") != 0) {
        perror("chdir");
        return 1;
    }

    if (mount("proc", "/proc", "proc", 0, NULL) != 0) {
        perror("mount /proc");
        return 1;
    }

    char *argv[] = {"/bin/sh", NULL};
    char *envp[] = {
        "HOME=/root",
        "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin",
        NULL
    };
    execve("/bin/sh", argv, envp);

    perror("execve");
    return 1;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <rootfs_path>\n", argv[0]);
        return 1;
    }

    char *rootfs = argv[1];

    char *stack = malloc(STACK_SIZE);
    if (!stack) {
        perror("malloc");
        return 1;
    }
    char *stack_top = stack + STACK_SIZE;

    int flags = CLONE_NEWPID
              | CLONE_NEWUTS
              | CLONE_NEWNS
              | SIGCHLD;

    pid_t pid = clone(container_main, stack_top, flags, rootfs);
    if (pid < 0) {
        perror("clone");
        free(stack);
        return 1;
    }

    // Fill metadata
    ContainerMeta *meta = &containers[num_containers++];
    snprintf(meta->id, sizeof(meta->id), "c%d", pid);
    meta->host_pid       = pid;
    meta->start_time     = time(NULL);
    meta->state          = STATE_RUNNING;
    meta->soft_limit_mb  = 50;
    meta->hard_limit_mb  = 100;
    snprintf(meta->log_path, sizeof(meta->log_path), "/tmp/log_%d.txt", pid);
    meta->exit_status    = -1;

    printf("[engine] Container '%s' started, host PID = %d\n", meta->id, pid);

    int status;
    waitpid(pid, &status, 0);

    meta->state       = STATE_STOPPED;
    meta->exit_status = WEXITSTATUS(status);
    printf("[engine] Container '%s' exited, status = %d\n", meta->id, meta->exit_status);

    free(stack);
    return 0;
}
