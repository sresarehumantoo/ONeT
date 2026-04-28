#ifndef ONET_PROC_H
#define ONET_PROC_H

/* Run argv[] via fork+execvp+waitpid. argv[0] is searched in PATH.
 * Returns the child's exit status (0 = success), or -1 on fork/exec failure. */
int proc_run(const char *const argv[]);

/* Same as proc_run, but stderr/stdout from the child are silenced.
 * Useful for commands whose failure is expected (e.g. iptables -D). */
int proc_run_quiet(const char *const argv[]);

/* fork+execvp a long-running daemon. Parent writes the child's PID to
 * pidfile and returns. Child detaches via setsid and redirects
 * stdin to /dev/null and stdout/stderr to logfile (created if missing).
 * Returns 0 on success. */
int proc_spawn_detached(const char *const argv[],
                        const char *pidfile,
                        const char *logfile);

/* Read PID from pidfile, send sig, poll for exit, then unlink pidfile.
 * Returns 0 on success, -1 if pidfile missing or unreadable. */
int proc_kill_pidfile(const char *pidfile, int sig);

/* Write "value\n" to path. Returns 0 on success. */
int proc_write_int(const char *path, int value);

#endif /* ONET_PROC_H */
