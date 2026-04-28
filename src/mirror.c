#include "mirror.h"

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

static int dummy_ensure(const char *name) {
    /* Idempotent: create if missing, then bring up. */
    const char *create[] = { "ip", "link", "add", "name", name,
                             "type", "dummy", NULL };
    proc_run_quiet(create);

    const char *up[] = { "ip", "link", "set", name, "up", NULL };
    return proc_run(up);
}

static void dummy_remove(const char *name) {
    const char *del[] = { "ip", "link", "del", name, NULL };
    proc_run_quiet(del);
}

static int want_ingress(const char *dir) {
    return strcmp(dir, "ingress") == 0 || strcmp(dir, "both") == 0;
}
static int want_egress(const char *dir) {
    return strcmp(dir, "egress") == 0 || strcmp(dir, "both") == 0;
}

static int install_one(const char *src, const char *dst, const char *dir) {
    if (want_ingress(dir)) {
        const char *qd[] = { "tc", "qdisc", "add", "dev", src,
                             "handle", "ffff:", "ingress", NULL };
        proc_run_quiet(qd);  /* may already exist */
        const char *fl[] = { "tc", "filter", "add", "dev", src,
                             "parent", "ffff:", "matchall",
                             "action", "mirred", "egress", "mirror",
                             "dev", dst, NULL };
        if (proc_run(fl) != 0) return -1;
    }
    if (want_egress(dir)) {
        const char *qd[] = { "tc", "qdisc", "replace", "dev", src,
                             "root", "handle", "1:", "prio", NULL };
        if (proc_run(qd) != 0) return -1;
        const char *fl[] = { "tc", "filter", "add", "dev", src,
                             "parent", "1:", "matchall",
                             "action", "mirred", "egress", "mirror",
                             "dev", dst, NULL };
        if (proc_run(fl) != 0) return -1;
    }
    return 0;
}

static void remove_one(const char *src, const char *dir) {
    if (want_ingress(dir)) {
        const char *del[] = { "tc", "qdisc", "del", "dev", src,
                              "ingress", NULL };
        proc_run_quiet(del);
    }
    if (want_egress(dir)) {
        const char *del[] = { "tc", "qdisc", "del", "dev", src,
                              "root", NULL };
        proc_run_quiet(del);
    }
}

int mirror_install(const mirror_config_t *m) {
    if (!m->enable) return 0;
    if (m->destination[0] == '\0' || m->sources[0] == '\0') {
        log_warn("mirror enabled but destination or sources empty; skipping");
        return 0;
    }
    if (dummy_ensure(m->destination) < 0) {
        log_error("mirror: failed to bring up destination %s", m->destination);
        return -1;
    }
    const char *dir = m->direction[0] ? m->direction : "both";

    char buf[256];
    snprintf(buf, sizeof buf, "%s", m->sources);
    char *tok = strtok(buf, ",");
    int rc = 0, n = 0;
    while (tok) {
        char *src = trim(tok);
        if (*src) {
            if (install_one(src, m->destination, dir) < 0) {
                log_warn("mirror: failed on %s -> %s", src, m->destination);
                rc = -1;
            } else {
                n++;
            }
        }
        tok = strtok(NULL, ",");
    }
    log_info("mirror: %s -> %s (%s, %d sources)",
             m->sources, m->destination, dir, n);
    return rc;
}

int mirror_remove(const mirror_config_t *m) {
    if (!m->enable) return 0;
    const char *dir = m->direction[0] ? m->direction : "both";
    char buf[256];
    snprintf(buf, sizeof buf, "%s", m->sources);
    char *tok = strtok(buf, ",");
    while (tok) {
        char *src = trim(tok);
        if (*src) remove_one(src, dir);
        tok = strtok(NULL, ",");
    }
    if (m->destination[0]) dummy_remove(m->destination);
    return 0;
}
