#ifndef ONET_SERVICES_H
#define ONET_SERVICES_H

#include "config.h"

/* dnsmasq drop-in: appended once per interface; restart picks up changes. */
int dnsmasq_install(const iface_config_t *iface);
int dnsmasq_remove(void);
int dnsmasq_restart(void);

/* hostapd: write conf, spawn detached daemon, terminate via pidfile. */
int hostapd_write_conf(const iface_config_t *iface,
                       const global_config_t *g,
                       char out_path[256]);
int hostapd_start(const char *conf_path, const char *iface_name);
int hostapd_stop(const char *iface_name);

/* iptables FORWARD rules between LAN iface and WAN (upstream tether). */
int iptables_forward_install(const char *lan, const char *wan);
int iptables_forward_remove(const char *lan, const char *wan);

#endif /* ONET_SERVICES_H */
