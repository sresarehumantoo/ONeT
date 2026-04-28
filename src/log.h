#ifndef ONET_LOG_H
#define ONET_LOG_H

/* Tiny logger. Routes to syslog when backend is set to syslog at init,
 * otherwise to stderr with a [LEVEL] prefix.
 *
 * Plain function names (not macros) to avoid colliding with <syslog.h>'s
 * LOG_INFO/LOG_DEBUG priority constants. */

enum {
    ONET_LOG_STDERR = 0,
    ONET_LOG_SYSLOG = 1,
};

void log_init(int backend);
void log_close(void);

void log_info(const char *fmt, ...)  __attribute__((format(printf, 1, 2)));
void log_warn(const char *fmt, ...)  __attribute__((format(printf, 1, 2)));
void log_error(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void log_debug(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif /* ONET_LOG_H */
