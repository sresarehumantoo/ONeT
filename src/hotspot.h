#ifndef ONET_HOTSPOT_H
#define ONET_HOTSPOT_H

#include "config.h"

int  hotspot_up(const iface_config_t *iface, const global_config_t *g);
int  hotspot_down(const iface_config_t *iface, const global_config_t *g);
void hotspot_kill_existing(void);

#endif /* ONET_HOTSPOT_H */
