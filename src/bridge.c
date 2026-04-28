#include "bridge.h"

#include <stdio.h>
#include <string.h>

#include "log.h"
#include "proc.h"

static char *trim(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n')) *--e = '\0';
    return s;
}

int bridge_create(const char *name, const char *members_csv) {
    /* Create (ignore "exists"). */
    const char *create[] = { "ip", "link", "add", "name", name,
                             "type", "bridge", NULL };
    proc_run_quiet(create);

    const char *up[] = { "ip", "link", "set", name, "up", NULL };
    if (proc_run(up) != 0) {
        log_error("failed to bring bridge %s up", name);
        return -1;
    }

    if (!members_csv || members_csv[0] == '\0') return 0;

    char buf[256];
    snprintf(buf, sizeof buf, "%s", members_csv);
    char *tok = strtok(buf, ",");
    int rc = 0;
    while (tok) {
        char *m = trim(tok);
        if (*m) {
            const char *enslave[] = { "ip", "link", "set", m,
                                      "master", name, NULL };
            if (proc_run(enslave) != 0) {
                log_warn("could not add %s to bridge %s", m, name);
                rc = -1;
            }
            const char *mup[] = { "ip", "link", "set", m, "up", NULL };
            proc_run_quiet(mup);
        }
        tok = strtok(NULL, ",");
    }
    return rc;
}

int bridge_destroy(const char *name) {
    const char *down[] = { "ip", "link", "set", name, "down", NULL };
    proc_run_quiet(down);
    const char *del[]  = { "ip", "link", "del", name, NULL };
    proc_run_quiet(del);
    return 0;
}
