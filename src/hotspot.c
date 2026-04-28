#include "hotspot.h"

#include <stdio.h>
#include <string.h>

#include "bridge.h"
#include "log.h"
#include "netif.h"
#include "proc.h"
#include "services.h"
#include "wan.h"

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

int hotspot_wan_up(const global_config_t *g) {
    if (g->wan.name[0] == '\0') {
        log_error("no WAN interface configured (set [Wan].name or legacy [Hotspot].fwd_iface)");
        return -1;
    }
    if (wan_link_up(&g->wan) < 0) {
        log_error("failed to bring WAN link up");
        return -1;
    }
    if (proc_write_int("/proc/sys/net/ipv4/ip_forward", 1) < 0) return -1;
    if (g->v6.enable) {
        if (proc_write_int("/proc/sys/net/ipv6/conf/all/forwarding", 1) < 0) {
            log_warn("failed to enable IPv6 forwarding (kernel built without v6?)");
        }
        ip6tables_forward_install();
    }

    if (g->wan.nat) {
        if (iptables_nat_install(g->wan.name) < 0) {
            log_error("failed to install NAT MASQUERADE on %s", g->wan.name);
            return -1;
        }
        log_info("NAT enabled on WAN %s", g->wan.name);
    } else {
        log_info("NAT disabled (wan.type=%s)", g->wan.type);
    }

    if (iptables_input_install(g->wan.name, g->fw.input_drop_wan) < 0) {
        log_warn("failed to install INPUT firewall (continuing)");
    }

    if (iptables_port_forwards_install(g->wan.name) < 0) {
        log_warn("some port forwards failed to install");
    }

    if (qos_install(g->wan.name, g->wan.qos) < 0) {
        log_warn("QoS qdisc %s failed on %s (continuing)", g->wan.qos, g->wan.name);
    } else if (strcmp(g->wan.qos, "none") != 0) {
        log_info("QoS %s active on %s", g->wan.qos, g->wan.name);
    }

    return 0;
}

int hotspot_wan_down(const global_config_t *g) {
    if (g->wan.name[0] == '\0') return 0;
    qos_remove(g->wan.name);
    iptables_port_forwards_remove(g->wan.name);
    iptables_input_remove();
    if (g->wan.nat) iptables_nat_remove(g->wan.name);
    if (g->v6.enable) ip6tables_forward_remove();
    wan_link_down(&g->wan);
    return 0;
}

int hotspot_up(const iface_config_t *iface, const global_config_t *g) {
    if (!iface->enabled) {
        log_info("iface %s disabled, skipping", iface->name);
        return 0;
    }

    /* Case A: this iface declares bridge_members → it IS the bridge. */
    if (iface->bridge_members[0] != '\0') {
        log_info("bringing up bridge %s with members [%s]",
                 iface->name, iface->bridge_members);
        if (bridge_create(iface->name, iface->bridge_members) < 0) return -1;
        /* The bridge gets normal L3 + dnsmasq + FORWARD treatment below. */
    }

    /* Case B: this iface joins a bridge owned by another .int. Hostapd
     * handles the L2 join via bridge=. The bridge owns IP/dnsmasq/iptables. */
    if (iface->bridge[0] != '\0') {
        if (!netif_exists(iface->name)) {
            log_info("iface %s not present, skipping", iface->name);
            return 0;
        }
        if (!netif_is_wireless(iface->name)) {
            log_warn("%s has bridge=%s but is not wireless; nothing to do",
                     iface->name, iface->bridge);
            return 0;
        }
        char conf_path[256];
        if (hostapd_write_conf(iface, g, conf_path) < 0) return -1;
        if (hostapd_start(conf_path, iface->name) < 0) {
            log_error("failed to start hostapd for %s", iface->name);
            return -1;
        }
        log_info("hostapd started for %s on bridge %s",
                 iface->name, iface->bridge);
        return 0;
    }

    /* Case C: standalone L3 interface (or a freshly-created bridge). */
    if (!netif_exists(iface->name)) {
        log_info("iface %s not present, skipping", iface->name);
        return 0;
    }

    int wireless = netif_is_wireless(iface->name);
    log_info("iface %s: %s, bringing up (%s -> %s)",
        iface->name, wireless ? "wireless"
                              : (iface->bridge_members[0] ? "bridge" : "wired"),
        iface->range_start, iface->range_stop);

    if (dnsmasq_install(iface, g) < 0) return -1;
    ifconfig_down(iface->name);
    if (iptables_forward_install(iface->name, g->wan.name) < 0) return -1;
    if (ifconfig_up(iface->name, iface->ip, iface->mask) < 0) return -1;
    route_del_default(iface->name);

    if (g->v6.enable && iface->ipv6) {
        ipv6_addr_install(iface->name, g->v6.ula_prefix);
        log_info("IPv6 ULA address installed on %s (prefix %s)",
                 iface->name, g->v6.ula_prefix);
    }

    if (wireless) {
        char conf_path[256];
        if (hostapd_write_conf(iface, g, conf_path) < 0) return -1;
        if (hostapd_start(conf_path, iface->name) < 0) {
            log_error("failed to start hostapd for %s", iface->name);
            return -1;
        }
        log_info("hostapd started for %s (conf: %s)", iface->name, conf_path);
    }
    return 0;
}

int hotspot_down(const iface_config_t *iface, const global_config_t *g) {
    if (netif_is_wireless(iface->name)) {
        hostapd_stop(iface->name);
    }
    if (iface->bridge[0] != '\0') {
        /* Bridge owner handles L3 teardown; nothing else to do here. */
        return 0;
    }
    iptables_forward_remove(iface->name, g->wan.name);
    if (g->v6.enable && iface->ipv6) {
        ipv6_addr_remove(iface->name, g->v6.ula_prefix);
    }
    ifconfig_down(iface->name);
    if (iface->bridge_members[0] != '\0') {
        bridge_destroy(iface->name);
    }
    log_info("iface %s torn down", iface->name);
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
