#include "proc.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static int run_with_redirect(const char *const argv[], int silence) {
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return -1;
    }
    if (pid == 0) {
        if (silence) {
            int devnull = open("/dev/null", O_WRONLY);
            if (devnull >= 0) {
                dup2(devnull, STDOUT_FILENO);
                dup2(devnull, STDERR_FILENO);
                close(devnull);
            }
        }
        execvp(argv[0], (char *const *)argv);
        fprintf(stderr, "execvp %s: %s\n", argv[0], strerror(errno));
        _exit(127);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        perror("waitpid");
        return -1;
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return -1;
}

int proc_run(const char *const argv[]) {
    return run_with_redirect(argv, 0);
}

int proc_run_quiet(const char *const argv[]) {
    return run_with_redirect(argv, 1);
}

int proc_spawn_detached(const char *const argv[],
                        const char *pidfile,
                        const char *logfile) {
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return -1;
    }
    if (pid == 0) {
        if (setsid() < 0) _exit(127);
        int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            close(devnull);
        }
        int log = open(logfile, O_WRONLY | O_CREAT | O_APPEND, 0640);
        if (log >= 0) {
            dup2(log, STDOUT_FILENO);
            dup2(log, STDERR_FILENO);
            close(log);
        }
        execvp(argv[0], (char *const *)argv);
        _exit(127);
    }
    FILE *f = fopen(pidfile, "w");
    if (!f) {
        perror(pidfile);
        return -1;
    }
    fprintf(f, "%d\n", (int)pid);
    fclose(f);
    return 0;
}

int proc_kill_pidfile(const char *pidfile, int sig) {
    FILE *f = fopen(pidfile, "r");
    if (!f) return -1;
    int pid = 0;
    int n = fscanf(f, "%d", &pid);
    fclose(f);
    if (n != 1 || pid <= 1) return -1;

    if (kill(pid, sig) < 0 && errno != ESRCH) {
        perror("kill");
        return -1;
    }
    /* Best-effort wait up to ~5s for exit. */
    for (int i = 0; i < 50; i++) {
        if (kill(pid, 0) < 0 && errno == ESRCH) break;
        usleep(100 * 1000);
    }
    unlink(pidfile);
    return 0;
}

int proc_write_int(const char *path, int value) {
    FILE *f = fopen(path, "w");
    if (!f) {
        perror(path);
        return -1;
    }
    fprintf(f, "%d\n", value);
    fclose(f);
    return 0;
}
