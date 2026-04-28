#include "wan.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "log.h"
#include "proc.h"

/* Run `mmcli -L` and return the first numeric modem index found, or -1. */
static int detect_modem_index(void) {
    /* Capture mmcli -L into a pipe. */
    int fd[2];
    if (pipe(fd) < 0) return -1;
    pid_t pid = fork();
    if (pid < 0) { close(fd[0]); close(fd[1]); return -1; }
    if (pid == 0) {
        close(fd[0]);
        dup2(fd[1], 1);
        close(fd[1]);
        execlp("mmcli", "mmcli", "-L", NULL);
        _exit(127);
    }
    close(fd[1]);

    char buf[1024];
    ssize_t n = read(fd[0], buf, sizeof buf - 1);
    close(fd[0]);
    /* Reap the child. */
    int st;
    while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
    if (n <= 0) return -1;
    buf[n] = '\0';

    /* Look for "/Modem/<N>". */
    const char *p = strstr(buf, "/Modem/");
    if (!p) return -1;
    p += strlen("/Modem/");
    int idx = 0, any = 0;
    while (isdigit((unsigned char)*p)) { idx = idx * 10 + (*p++ - '0'); any = 1; }
    return any ? idx : -1;
}

int wan_link_up(const wan_config_t *w) {
    if (strcmp(w->type, "wwan") != 0) return 0;
    if (w->modem_apn[0] == '\0') {
        log_error("wan.type=wwan but modem_apn is empty");
        return -1;
    }
    int idx = -1;
    if (w->modem_index[0] != '\0') {
        idx = atoi(w->modem_index);
    } else {
        idx = detect_modem_index();
        if (idx < 0) {
            log_error("could not auto-detect modem (mmcli -L returned nothing)");
            return -1;
        }
        log_info("auto-detected modem index %d", idx);
    }

    char idx_arg[16], simple[128];
    snprintf(idx_arg, sizeof idx_arg, "%d", idx);
    snprintf(simple, sizeof simple, "apn=%s", w->modem_apn);

    /* Ensure the modem is enabled, then connect. Each command has its own
     * exec — mmcli doesn't take chained ops. */
    const char *enable[] = { "mmcli", "-m", idx_arg, "--enable", NULL };
    proc_run_quiet(enable);  /* often already enabled */

    const char *connect[] = { "mmcli", "-m", idx_arg,
                              "--simple-connect", simple, NULL };
    if (proc_run(connect) != 0) {
        log_error("mmcli simple-connect failed for modem %d", idx);
        return -1;
    }
    log_info("modem %d connected (apn=%s, iface=%s)", idx, w->modem_apn, w->name);
    return 0;
}

int wan_link_down(const wan_config_t *w) {
    if (strcmp(w->type, "wwan") != 0) return 0;
    int idx = -1;
    if (w->modem_index[0] != '\0') idx = atoi(w->modem_index);
    else idx = detect_modem_index();
    if (idx < 0) return 0;

    char idx_arg[16];
    snprintf(idx_arg, sizeof idx_arg, "%d", idx);
    const char *disconnect[] = { "mmcli", "-m", idx_arg,
                                 "--simple-disconnect", NULL };
    proc_run_quiet(disconnect);
    return 0;
}
