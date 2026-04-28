#include "log.h"

#include <stdarg.h>
#include <stdio.h>
#include <syslog.h>

static int g_backend = ONET_LOG_STDERR;

void log_init(int backend) {
    g_backend = backend;
    if (backend == ONET_LOG_SYSLOG) {
        openlog("onet", LOG_PID | LOG_NDELAY, LOG_DAEMON);
    }
}

void log_close(void) {
    if (g_backend == ONET_LOG_SYSLOG) closelog();
}

static void emit(int prio, const char *label, const char *fmt, va_list ap) {
    if (g_backend == ONET_LOG_SYSLOG) {
        vsyslog(prio, fmt, ap);
        return;
    }
    fprintf(stderr, "[%s] ", label);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
}

void log_info(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    emit(LOG_INFO, "INFO", fmt, ap);
    va_end(ap);
}

void log_warn(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    emit(LOG_WARNING, "WARN", fmt, ap);
    va_end(ap);
}

void log_error(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    emit(LOG_ERR, "ERROR", fmt, ap);
    va_end(ap);
}

void log_debug(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    emit(LOG_DEBUG, "DEBUG", fmt, ap);
    va_end(ap);
}
