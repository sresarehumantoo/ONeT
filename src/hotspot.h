#ifndef ONET_HOTSPOT_H
#define ONET_HOTSPOT_H

#include "config.h"

/* Once-per-WAN setup/teardown: NAT, INPUT firewall, port forwards, QoS.
 * Call hotspot_wan_up() before iterating LAN ifaces with hotspot_up(),
 * and hotspot_wan_down() after iterating LAN ifaces with hotspot_down(). */
int  hotspot_wan_up(const global_config_t *g);
int  hotspot_wan_down(const global_config_t *g);

int  hotspot_up(const iface_config_t *iface, const global_config_t *g);
int  hotspot_down(const iface_config_t *iface, const global_config_t *g);
void hotspot_kill_existing(void);

/* Switch the active WAN to new_idx (0=primary, 1=backup). Tears down
 * per-LAN FORWARD + WAN-side rules on the old WAN, mutates
 * g->active_wan_idx, brings up the new one, reinstalls per-LAN FORWARD,
 * and restarts dhcpcd-PD if active. Rolls back on failure. */
int  hotspot_wan_switch(global_config_t *g, int new_idx);

#endif /* ONET_HOTSPOT_H */
