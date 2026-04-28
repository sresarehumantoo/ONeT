#include "netif.h"

/* glibc's <net/if.h> must precede any linux kernel header that drags in
 * <linux/if.h>, otherwise IFF_* enums and struct ifreq get redefined. */
#include <net/if.h>

#include <ifaddrs.h>
#include <linux/wireless.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

bool netif_exists(const char *ifname) {
    struct ifaddrs *ifaddr = NULL;
    if (getifaddrs(&ifaddr) < 0) return false;
    bool found = false;
    for (struct ifaddrs *ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL) continue;
        if (ifa->ifa_addr->sa_family != AF_PACKET) continue;
        if (strcmp(ifa->ifa_name, ifname) == 0) {
            found = true;
            break;
        }
    }
    freeifaddrs(ifaddr);
    return found;
}

bool netif_is_wireless(const char *ifname) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return false;

    struct iwreq pwrq;
    memset(&pwrq, 0, sizeof pwrq);
    snprintf(pwrq.ifr_name, sizeof pwrq.ifr_name, "%s", ifname);

    bool wireless = (ioctl(sock, SIOCGIWNAME, &pwrq) != -1);
    close(sock);
    return wireless;
}
