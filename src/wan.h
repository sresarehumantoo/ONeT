#ifndef ONET_WAN_H
#define ONET_WAN_H

#include "config.h"

/* Bring the WAN link itself up (currently only used for type=wwan).
 * For tether/ethernet types this is a no-op — the interface is assumed
 * to be brought up by ModemManager / kernel / EasyTether app already. */
int wan_link_up(const wan_config_t *w);

/* Mirror tear-down. Disconnects the modem on type=wwan. */
int wan_link_down(const wan_config_t *w);

#endif /* ONET_WAN_H */
