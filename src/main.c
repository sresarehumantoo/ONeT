#include <dirent.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "config.h"
#include "hotspot.h"
#include "log.h"
#include "proc.h"
#include "services.h"

static int parse_ext(const struct dirent *dir) {
    if (!dir || dir->d_type != DT_REG) return 0;
    const char *ext = strrchr(dir->d_name, '.');
    return ext && strcmp(ext, ".int") == 0;
}

static void usage(const char *argv0) {
    fprintf(stderr,
        "Usage: %s {-s|-w|-g|-m|-h} [-d]\n"
        "\n"
        "  -s   Start hotspot for every enabled .int file in %s\n"
        "  -w   Tear down (wipe) anything -s configured\n"
        "  -g   Generate a default interface config at %s and exit\n"
        "  -m   Run watchdog: ping [Wan].watchdog_target, recover on failure\n"
        "  -h   Show this help\n"
        "  -d   With -s: do not kill existing wpa_supplicant/hostapd\n",
        argv0, ONET_CONFIG_DIR, ONET_DEFAULT_INT);
}

static volatile sig_atomic_t g_stop = 0;
static void on_sigterm(int sig) { (void)sig; g_stop = 1; }

typedef int (*iface_fn)(const iface_config_t *, const global_config_t *);
typedef int (*iface_filter)(const iface_config_t *);

/* Two-pass driver: bridges (those with bridge_members) up first so that
 * bridge-joining wireless ifaces can reference an existing bridge. */
static int is_bridge(const iface_config_t *i)     { return i->bridge_members[0] != '\0'; }
static int is_not_bridge(const iface_config_t *i) { return i->bridge_members[0] == '\0'; }

static int iter_iface_configs_filtered(iface_fn fn, iface_filter pass,
                                       const global_config_t *g);

static int iter_iface_configs(iface_fn fn, const global_config_t *g) {
    int rc1 = iter_iface_configs_filtered(fn, is_bridge,     g);
    int rc2 = iter_iface_configs_filtered(fn, is_not_bridge, g);
    return (rc1 < 0 || rc2 < 0) ? -1 : 0;
}

static int iter_iface_configs_filtered(iface_fn fn, iface_filter pass,
                                       const global_config_t *g) {
    struct dirent **list = NULL;
    int n = scandir(ONET_CONFIG_DIR, &list, parse_ext, alphasort);
    if (n < 0) {
        log_error("scandir %s: %m", ONET_CONFIG_DIR);
        return -1;
    }
    if (n == 0) {
        log_info("no .int interface configs in %s", ONET_CONFIG_DIR);
        free(list);
        return 0;
    }
    int rc = 0;
    for (int i = 0; i < n; i++) {
        char path[512];
        snprintf(path, sizeof path, "%s/%s",
                 ONET_CONFIG_DIR, list[i]->d_name);
        log_info("processing %s", list[i]->d_name);

        iface_config_t iface;
        if (config_load_iface(path, &iface) < 0) {
            log_error("failed to load %s, skipping", path);
            free(list[i]);
            continue;
        }
        if (pass(&iface)) {
            if (fn(&iface, g) != 0) rc = -1;
        }
        free(list[i]);
    }
    free(list);
    return rc;
}

int main(int argc, char *argv[]) {
    int s_flag = 0, d_flag = 0, w_flag = 0, h_flag = 0, g_flag = 0, m_flag = 0;
    int opt;
    while ((opt = getopt(argc, argv, "sdwhgm")) != -1) {
        switch (opt) {
            case 's': s_flag = 1; break;
            case 'd': d_flag = 1; break;
            case 'w': w_flag = 1; break;
            case 'h': h_flag = 1; break;
            case 'g': g_flag = 1; break;
            case 'm': m_flag = 1; break;
            default:  usage(argv[0]); return 2;
        }
    }
    if (argc <= 1 || h_flag) {
        usage(argv[0]);
        return h_flag ? 0 : 2;
    }
    if (s_flag + w_flag + g_flag + m_flag > 1) {
        log_error("-s, -w, -g, -m are mutually exclusive");
        return 2;
    }
    /* Use syslog when running detached (no controlling tty), stderr otherwise. */
    log_init(isatty(STDERR_FILENO) ? ONET_LOG_STDERR : ONET_LOG_SYSLOG);

    if (geteuid() != 0) {
        log_error("ONeT must be run as root");
        return 1;
    }

    if (config_ensure_dirs() < 0) return 1;

    /* Bootstrap a global config the first time we run. */
    if (access(ONET_GLOBAL_INI, F_OK) != 0) {
        global_config_t defaults;
        config_default_global(&defaults);
        if (config_save_global(ONET_GLOBAL_INI, &defaults) < 0) {
            log_error("failed to write %s", ONET_GLOBAL_INI);
            return 1;
        }
        log_info("created default %s — edit SSID/PSK before -s", ONET_GLOBAL_INI);
    }

    if (g_flag) {
        iface_config_t iface;
        config_default_iface(&iface);
        if (config_save_iface(ONET_DEFAULT_INT, &iface) < 0) {
            log_error("failed to write %s", ONET_DEFAULT_INT);
            return 1;
        }
        log_info("wrote %s", ONET_DEFAULT_INT);
        return 0;
    }

    global_config_t g;
    if (config_load_global(ONET_GLOBAL_INI, &g) < 0) {
        log_error("failed to load %s", ONET_GLOBAL_INI);
        return 1;
    }

    if (s_flag) {
        if (!d_flag) hotspot_kill_existing();
        if (hotspot_wan_up(&g) < 0) return 1;
        int rc = iter_iface_configs(hotspot_up, &g);
        if (rc == 0) dnsmasq_restart();
        return rc < 0 ? 1 : 0;
    }
    if (w_flag) {
        iter_iface_configs(hotspot_down, &g);
        hotspot_wan_down(&g);
        dnsmasq_remove();
        dnsmasq_restart();
        proc_write_int("/proc/sys/net/ipv4/ip_forward", 0);
        return 0;
    }
    if (m_flag) {
        signal(SIGTERM, on_sigterm);
        signal(SIGINT,  on_sigterm);

        int interval = g.wan.watchdog_interval_s > 0 ? g.wan.watchdog_interval_s : 30;
        int max_fails = g.wan.watchdog_failures > 0 ? g.wan.watchdog_failures : 3;
        int consec = 0;

        log_info("watchdog: target=%s interval=%ds threshold=%d",
                 g.wan.watchdog_target, interval, max_fails);

        while (!g_stop) {
            const char *ping[] = {
                "ping", "-c", "1", "-W", "2",
                g.wan.watchdog_target, NULL,
            };
            int rc = proc_run_quiet(ping);
            if (rc == 0) {
                if (consec > 0) log_info("watchdog: target reachable again");
                consec = 0;
            } else {
                consec++;
                log_warn("watchdog: ping %s failed (%d/%d)",
                         g.wan.watchdog_target, consec, max_fails);
                if (consec >= max_fails) {
                    log_warn("watchdog: triggering full recovery cycle");
                    iter_iface_configs(hotspot_down, &g);
                    hotspot_wan_down(&g);
                    dnsmasq_remove();
                    if (hotspot_wan_up(&g) < 0) {
                        log_error("watchdog: WAN recovery failed; will retry");
                    } else {
                        iter_iface_configs(hotspot_up, &g);
                        dnsmasq_restart();
                    }
                    consec = 0;
                }
            }
            for (int i = 0; i < interval && !g_stop; i++) sleep(1);
        }
        log_info("watchdog: shutting down");
        return 0;
    }

    usage(argv[0]);
    return 2;
}
