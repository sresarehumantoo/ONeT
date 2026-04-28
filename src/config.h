#ifndef ONET_CONFIG_H
#define ONET_CONFIG_H

#include <net/if.h>      /* IFNAMSIZ */
#include <netinet/in.h>  /* INET_ADDRSTRLEN */

/* Path defaults are guarded so they can be overridden via -D at build time. */
#ifndef ONET_DIR
#define ONET_DIR         "/etc/ONeT"
#endif
#ifndef ONET_HOTSPOT_DIR
#define ONET_HOTSPOT_DIR "/etc/ONeT/hotspot"
#endif
#ifndef ONET_CONFIG_DIR
#define ONET_CONFIG_DIR  "/etc/ONeT/hotspot/config"
#endif
#ifndef ONET_GLOBAL_INI
#define ONET_GLOBAL_INI  "/etc/ONeT/hotspot/custom.ini"
#endif
#ifndef ONET_DEFAULT_INT
#define ONET_DEFAULT_INT "/etc/ONeT/hotspot/config/default.int"
#endif
#ifndef ONET_RUN_DIR
#define ONET_RUN_DIR     "/run/ONeT"
#endif
#ifndef ONET_LOG_DIR
#define ONET_LOG_DIR     "/var/log/ONeT"
#endif
#ifndef DNSMASQ_DROPIN
#define DNSMASQ_DROPIN   "/etc/dnsmasq.d/onet.conf"
#endif

/* hostapd: SSID 1..32, PSK 8..63 */
typedef struct {
    char ssid[33];
    char psk[64];
    char fwd_iface[IFNAMSIZ];
    char country[3];
} global_config_t;

typedef struct {
    char name[IFNAMSIZ];
    char ip[INET_ADDRSTRLEN];
    char mask[INET_ADDRSTRLEN];
    char range_start[INET_ADDRSTRLEN];
    char range_stop[INET_ADDRSTRLEN];
    char dns0[INET_ADDRSTRLEN];
    char dns1[INET_ADDRSTRLEN];
    char lease_time[16];
    char channel[8];
    char phy_mode[4];     /* "n", "ac", "ax", "be" */
    int  chwidth_mhz;     /* 20, 40, 80, 160, 320 */
    int  enabled;         /* 0/1 */
    int  band;            /* 0 = 2.4 GHz, 1 = 5 GHz, 2 = 6 GHz */
} iface_config_t;

void config_default_global(global_config_t *cfg);
void config_default_iface(iface_config_t *cfg);

int config_load_global(const char *path, global_config_t *cfg);
int config_save_global(const char *path, const global_config_t *cfg);

int config_load_iface(const char *path, iface_config_t *cfg);
int config_save_iface(const char *path, const iface_config_t *cfg);

int config_ensure_dirs(void);

#endif /* ONET_CONFIG_H */
