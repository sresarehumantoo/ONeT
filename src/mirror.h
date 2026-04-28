#ifndef ONET_MIRROR_H
#define ONET_MIRROR_H

#include "config.h"

/* SPAN-style passive mirror via tc mirred. Copies frames from each source
 * interface to the destination interface; user runs tcpdump/zeek/etc. on
 * the destination. Destination is auto-created as a `dummy` if missing. */
int mirror_install(const mirror_config_t *m);
int mirror_remove(const mirror_config_t *m);

#endif /* ONET_MIRROR_H */
