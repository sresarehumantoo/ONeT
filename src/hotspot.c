#include "hotspot.h"

#include <stdio.h>

#include "netif.h"
#include "proc.h"
#include "services.h"

static int ifconfig_up(const char *name, const char *ip, const char *mask) {
    const char *argv[] = { "ifconfig", name, ip, "netmask", mask, "up", NULL };
    return proc_run(argv);
}

static int ifconfig_down(const char *name) {
    const char *argv[] = { "ifconfig", name, "down", NULL };
    return proc_run_quiet(argv);
}

static int route_del_default(const char *name) {
    const char *argv[] = { "ip", "route", "del", "default", "dev", name, NULL };
    return proc_run_quiet(argv);
}

int hotspot_up(const iface_config_t *iface, const global_config_t *g) {
    if (!iface->enabled) {
        printf("Interface %s: disabled, skipping\n", iface->name);
        return 0;
    }
    if (!netif_exists(iface->name)) {
        printf("Interface %s: not present on system, skipping\n", iface->name);
        return 0;
    }

    int wireless = netif_is_wireless(iface->name);
    printf("Interface %s: %s, bringing up (%s -> %s)\n",
        iface->name,
        wireless ? "wireless" : "wired",
        iface->range_start, iface->range_stop);

    if (proc_write_int("/proc/sys/net/ipv4/ip_forward", 1) < 0) return -1;
    if (dnsmasq_install(iface) < 0) return -1;

    ifconfig_down(iface->name);
    if (iptables_forward_install(iface->name, g->fwd_iface) < 0) return -1;
    if (ifconfig_up(iface->name, iface->ip, iface->mask) < 0) return -1;
    route_del_default(iface->name);

    if (wireless) {
        char conf_path[256];
        if (hostapd_write_conf(iface, g, conf_path) < 0) return -1;
        if (hostapd_start(conf_path, iface->name) < 0) {
            fprintf(stderr, "Failed to start hostapd for %s\n", iface->name);
            return -1;
        }
        printf("hostapd started for %s (conf: %s)\n", iface->name, conf_path);
    }
    return 0;
}

int hotspot_down(const iface_config_t *iface, const global_config_t *g) {
    if (netif_is_wireless(iface->name)) {
        hostapd_stop(iface->name);
    }
    iptables_forward_remove(iface->name, g->fwd_iface);
    ifconfig_down(iface->name);
    printf("Interface %s: torn down\n", iface->name);
    return 0;
}

void hotspot_kill_existing(void) {
    const char *killall[] = {
        "killall", "-q", "wpa_supplicant", "hostapd", NULL,
    };
    proc_run_quiet(killall);

    const char *rfkill[] = { "rfkill", "unblock", "wlan", NULL };
    proc_run_quiet(rfkill);

    const char *netd[] = {
        "systemctl", "restart",
        "systemd-networkd", "systemd-resolved", NULL,
    };
    proc_run_quiet(netd);

    const char *reload[] = { "systemctl", "daemon-reload", NULL };
    proc_run_quiet(reload);

    dnsmasq_remove();
}
