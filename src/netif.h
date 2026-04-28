#ifndef ONET_NETIF_H
#define ONET_NETIF_H

#include <stdbool.h>

bool netif_exists(const char *ifname);
bool netif_is_wireless(const char *ifname);

#endif /* ONET_NETIF_H */
