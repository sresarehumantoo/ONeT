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
    const wan_config_t *w = wan_active(g);
    if (w->name[0] == '\0') {
        log_error("no WAN interface configured (set [Wan].name or legacy [Hotspot].fwd_iface)");
        return -1;
    }
    if (wan_link_up(w) < 0) {
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

    if (w->nat) {
        if (iptables_nat_install(w->name) < 0) {
            log_error("failed to install NAT MASQUERADE on %s", w->name);
            return -1;
        }
        log_info("NAT enabled on WAN %s", w->name);
    } else {
        log_info("NAT disabled (wan.type=%s)", w->type);
    }

    if (iptables_input_install(w->name, g->fw.input_drop_wan) < 0) {
        log_warn("failed to install INPUT firewall (continuing)");
    }

    if (iptables_port_forwards_install(w->name) < 0) {
        log_warn("some port forwards failed to install");
    }

    if (qos_install(w->name, w->qos) < 0) {
        log_warn("QoS qdisc %s failed on %s (continuing)", w->qos, w->name);
    } else if (strcmp(w->qos, "none") != 0) {
        log_info("QoS %s active on %s", w->qos, w->name);
    }

    return 0;
}

int hotspot_wan_down(const global_config_t *g) {
    const wan_config_t *w = wan_active(g);
    if (w->name[0] == '\0') return 0;
    qos_remove(w->name);
    iptables_port_forwards_remove(w->name);
    iptables_input_remove();
    if (w->nat) iptables_nat_remove(w->name);
    if (g->v6.enable) ip6tables_forward_remove();
    wan_link_down(w);
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
    if (iptables_forward_install(iface->name, wan_active(g)->name) < 0) return -1;
    if (ifconfig_up(iface->name, iface->ip, iface->mask) < 0) return -1;
    route_del_default(iface->name);

    if (g->v6.enable && iface->ipv6 && !g->v6.pd) {
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
    iptables_forward_remove(iface->name, wan_active(g)->name);
    if (g->v6.enable && iface->ipv6 && !g->v6.pd) {
        ipv6_addr_remove(iface->name, g->v6.ula_prefix);
    }
    ifconfig_down(iface->name);
    if (iface->bridge_members[0] != '\0') {
        bridge_destroy(iface->name);
    }
    log_info("iface %s torn down", iface->name);
    return 0;
}

/* Visitors used by hotspot_wan_switch to re-key per-LAN FORWARD rules. */
static int forward_install_visitor(const iface_config_t *i, void *u) {
    const global_config_t *g = (const global_config_t *)u;
    if (!i->enabled || i->bridge[0] != '\0') return 0;
    return iptables_forward_install(i->name, wan_active(g)->name);
}
static int forward_remove_visitor(const iface_config_t *i, void *u) {
    const global_config_t *g = (const global_config_t *)u;
    if (i->bridge[0] != '\0') return 0;
    iptables_forward_remove(i->name, wan_active(g)->name);
    return 0;
}

/* CSV collector reused to rebuild dhcpcd's LAN list during failover. */
typedef struct { char *buf; size_t cap, used; } pd_collector_t;
static int pd_visitor(const iface_config_t *i, void *u) {
    pd_collector_t *c = (pd_collector_t *)u;
    if (!i->enabled || !i->ipv6 || i->bridge[0] != '\0') return 0;
    int w = snprintf(c->buf + c->used, c->cap - c->used,
                     "%s%s", c->used ? "," : "", i->name);
    if (w > 0 && (size_t)w < c->cap - c->used) c->used += (size_t)w;
    return 0;
}

int hotspot_wan_switch(global_config_t *g, int new_idx) {
    if (new_idx < 0 || new_idx > 1) return -1;
    if (new_idx == g->active_wan_idx) return 0;
    if (new_idx == 1 && !wan_have_backup(g)) {
        log_error("cannot switch to backup WAN: [Wan.backup] not configured");
        return -1;
    }

    int old_idx = g->active_wan_idx;
    log_info("WAN switch: idx %d (%s) -> idx %d (%s)",
             old_idx, wan_active(g)->name,
             new_idx, (new_idx == 1 ? g->wan_backup.name : g->wan.name));

    /* 1) Pull down per-LAN FORWARD rules anchored on the old WAN. */
    config_for_each_iface(forward_remove_visitor, g);

    /* 2) Pull down WAN-side rules (NAT, INPUT, QoS, port-fwd, link). */
    hotspot_wan_down(g);

    /* 3) Flip the active index and bring up the new WAN. */
    g->active_wan_idx = new_idx;
    if (hotspot_wan_up(g) < 0) {
        log_error("WAN switch failed; rolling back to idx %d", old_idx);
        g->active_wan_idx = old_idx;
        if (hotspot_wan_up(g) < 0) {
            log_error("rollback also failed — manual intervention required");
        } else {
            config_for_each_iface(forward_install_visitor, g);
        }
        return -1;
    }

    /* 4) Reinstall per-LAN FORWARD rules anchored on the new WAN. */
    config_for_each_iface(forward_install_visitor, g);

    /* 5) Move dhcpcd-PD to the new WAN. */
    if (g->v6.enable && g->v6.pd) {
        char lans[512] = {0};
        pd_collector_t c = { lans, sizeof lans, 0 };
        config_for_each_iface(pd_visitor, &c);
        ipv6_pd_stop();
        ipv6_pd_start(g, lans);
    }
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
