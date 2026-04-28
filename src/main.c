#include <dirent.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "config.h"
#include "hotspot.h"
#include "log.h"
#include "mirror.h"
#include "proc.h"
#include "services.h"

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

/* Two-pass driver: bridges (those with bridge_members) up first so that
 * bridge-joining wireless ifaces can reference an existing bridge. */
static int is_bridge(const iface_config_t *i)     { return i->bridge_members[0] != '\0'; }
static int is_not_bridge(const iface_config_t *i) { return i->bridge_members[0] == '\0'; }

/* Visitor adapters for hotspot_up/down — visitor signature wants void*, so we
 * cast back to global_config_t* on the inside. */
static int hotspot_up_visitor(const iface_config_t *i, void *u) {
    return hotspot_up(i, (const global_config_t *)u);
}
static int hotspot_down_visitor(const iface_config_t *i, void *u) {
    return hotspot_down(i, (const global_config_t *)u);
}

/* Two-pass driver: bridges (those with bridge_members) come up first so that
 * bridge-joining wireless ifaces find an existing master regardless of file order. */
static int iter_iface_configs(config_iface_visitor_t fn, const global_config_t *g) {
    int rc1 = config_for_each_iface_filtered(fn, is_bridge,     (void *)g);
    int rc2 = config_for_each_iface_filtered(fn, is_not_bridge, (void *)g);
    return (rc1 < 0 || rc2 < 0) ? -1 : 0;
}

typedef struct {
    char  *buf;
    size_t buflen;
    size_t used;
} csv_collector_t;

/* Visitor that builds a CSV of LAN names eligible for a delegated /64. */
static int collect_pd_lan_visitor(const iface_config_t *i, void *u) {
    csv_collector_t *c = (csv_collector_t *)u;
    if (!i->enabled || !i->ipv6 || i->bridge[0] != '\0') return 0;
    int w = snprintf(c->buf + c->used, c->buflen - c->used,
                     "%s%s", c->used ? "," : "", i->name);
    if (w > 0 && (size_t)w < c->buflen - c->used) c->used += (size_t)w;
    return 0;
}

static void collect_pd_lans(const global_config_t *g, char *out, size_t outlen) {
    (void)g;
    out[0] = '\0';
    csv_collector_t c = { out, outlen, 0 };
    config_for_each_iface(collect_pd_lan_visitor, &c);
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
        int rc = iter_iface_configs(hotspot_up_visitor, &g);
        if (rc == 0 && g.v6.enable && g.v6.pd) {
            char lans[512];
            collect_pd_lans(&g, lans, sizeof lans);
            ipv6_pd_start(&g, lans);
        }
        if (rc == 0) mirror_install(&g.mirror);
        if (rc == 0) dnsmasq_restart();
        return rc < 0 ? 1 : 0;
    }
    if (w_flag) {
        mirror_remove(&g.mirror);
        ipv6_pd_stop();
        iter_iface_configs(hotspot_down_visitor, &g);
        hotspot_wan_down(&g);
        dnsmasq_remove();
        dnsmasq_restart();
        proc_write_int("/proc/sys/net/ipv4/ip_forward", 0);
        return 0;
    }
    if (m_flag) {
        signal(SIGTERM, on_sigterm);
        signal(SIGINT,  on_sigterm);

        int interval = wan_active(&g)->watchdog_interval_s > 0
                     ? wan_active(&g)->watchdog_interval_s : 30;
        int max_fails = wan_active(&g)->watchdog_failures > 0
                      ? wan_active(&g)->watchdog_failures : 3;
        int consec_fail = 0;
        int consec_pri_ok = 0;        /* failback streak on primary */
        time_t last_switch_t = time(NULL);

        log_info("watchdog: active=%s target=%s interval=%ds threshold=%d backup=%s",
                 wan_active(&g)->name, wan_active(&g)->watchdog_target,
                 interval, max_fails,
                 wan_have_backup(&g) ? g.wan_backup.name : "<none>");

        while (!g_stop) {
            const wan_config_t *act = wan_active(&g);
            const char *ping_act[] = {
                "ping", "-c", "1", "-W", "2", "-I", act->name,
                act->watchdog_target, NULL,
            };
            int rc = proc_run_quiet(ping_act);
            if (rc == 0) {
                if (consec_fail > 0)
                    log_info("watchdog: %s reachable again", act->name);
                consec_fail = 0;
            } else {
                consec_fail++;
                log_warn("watchdog: ping %s via %s failed (%d/%d)",
                         act->watchdog_target, act->name,
                         consec_fail, max_fails);
                if (consec_fail >= max_fails) {
                    if (wan_have_backup(&g)) {
                        int new_idx = (g.active_wan_idx == 0) ? 1 : 0;
                        log_warn("watchdog: failover to idx %d", new_idx);
                        if (hotspot_wan_switch(&g, new_idx) == 0) {
                            last_switch_t = time(NULL);
                            consec_fail = 0;
                            consec_pri_ok = 0;
                            dnsmasq_restart();
                        }
                    } else {
                        log_warn("watchdog: no backup; full-recovery cycle");
                        ipv6_pd_stop();
                        iter_iface_configs(hotspot_down_visitor, &g);
                        hotspot_wan_down(&g);
                        dnsmasq_remove();
                        if (hotspot_wan_up(&g) < 0) {
                            log_error("watchdog: WAN recovery failed; will retry");
                        } else {
                            iter_iface_configs(hotspot_up_visitor, &g);
                            if (g.v6.enable && g.v6.pd) {
                                char lans[512];
                                collect_pd_lans(&g, lans, sizeof lans);
                                ipv6_pd_start(&g, lans);
                            }
                            dnsmasq_restart();
                        }
                        consec_fail = 0;
                    }
                }
            }

            /* Failback: if we're on backup and primary is non-wwan, probe
             * primary's target via the primary iface. After 3 successes and
             * failback_hold_s elapsed since last switch, swap back. */
            if (g.active_wan_idx == 1
                && wan_have_backup(&g)
                && strcmp(g.wan.type, "wwan") != 0) {
                const wan_config_t *pri = &g.wan;
                const char *ping_pri[] = {
                    "ping", "-c", "1", "-W", "2", "-I", pri->name,
                    pri->watchdog_target, NULL,
                };
                if (proc_run_quiet(ping_pri) == 0) {
                    consec_pri_ok++;
                    int hold = g.failback_hold_s > 0 ? g.failback_hold_s : 300;
                    if (consec_pri_ok >= 3
                        && time(NULL) - last_switch_t >= hold) {
                        log_info("watchdog: failback to primary");
                        if (hotspot_wan_switch(&g, 0) == 0) {
                            last_switch_t = time(NULL);
                            consec_fail = 0;
                            consec_pri_ok = 0;
                            dnsmasq_restart();
                        }
                    }
                } else {
                    consec_pri_ok = 0;
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
