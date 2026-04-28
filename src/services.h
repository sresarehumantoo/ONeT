#ifndef ONET_SERVICES_H
#define ONET_SERVICES_H

#include "config.h"

/* dnsmasq drop-in: appended once per interface; restart picks up changes.
 * Reads global to decide whether to emit IPv6 RA stanzas. */
int dnsmasq_install(const iface_config_t *iface, const global_config_t *g);
int dnsmasq_remove(void);
int dnsmasq_restart(void);

/* IPv6: per-LAN ULA /64 install/remove. ula_prefix is a /48 like
 * "fd00:dead:beef" (3 hextets); subnet ID derived by hashing iface name. */
int ipv6_addr_install(const char *iface, const char *ula_prefix);
int ipv6_addr_remove(const char *iface, const char *ula_prefix);

/* ip6tables global rule: allow ULA-to-ULA forwarding so LAN segments can
 * reach each other over IPv6. Idempotent. */
int ip6tables_forward_install(void);
int ip6tables_forward_remove(void);

/* hostapd: write conf, spawn detached daemon, terminate via pidfile. */
int hostapd_write_conf(const iface_config_t *iface,
                       const global_config_t *g,
                       char out_path[256]);
int hostapd_start(const char *conf_path, const char *iface_name);
int hostapd_stop(const char *iface_name);

/* iptables FORWARD rules between LAN iface and WAN (upstream tether). */
int iptables_forward_install(const char *lan, const char *wan);
int iptables_forward_remove(const char *lan, const char *wan);

/* NAT: MASQUERADE all egress out of <wan>. Idempotent-ish via -D mirror. */
int iptables_nat_install(const char *wan);
int iptables_nat_remove(const char *wan);

/* INPUT chain: drop new connections from <wan>, allow established + LAN.
 * NOTE: this manipulates a custom chain ONET_INPUT, which we then jump to
 * from INPUT. Tear-down removes the jump and flushes the chain. */
int iptables_input_install(const char *wan, int drop_new_on_wan);
int iptables_input_remove(void);

/* Port forwards: read /etc/ONeT/hotspot/port_forwards.conf and apply
 * iptables nat PREROUTING DNAT + FORWARD ACCEPT rules. */
int iptables_port_forwards_install(const char *wan);
int iptables_port_forwards_remove(const char *wan);

/* QoS: tc qdisc add/del root <kind> on <wan>. kind ∈ {"fq_codel","cake","none"}. */
int qos_install(const char *wan, const char *kind);
int qos_remove(const char *wan);

#endif /* ONET_SERVICES_H */
