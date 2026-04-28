#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "config.h"
#include "hotspot.h"
#include "proc.h"
#include "services.h"

static int parse_ext(const struct dirent *dir) {
    if (!dir || dir->d_type != DT_REG) return 0;
    const char *ext = strrchr(dir->d_name, '.');
    return ext && strcmp(ext, ".int") == 0;
}

static void usage(const char *argv0) {
    fprintf(stderr,
        "Usage: %s {-s|-w|-g|-h} [-d]\n"
        "\n"
        "  -s   Start hotspot for every enabled .int file in %s\n"
        "  -w   Tear down (wipe) anything -s configured\n"
        "  -g   Generate a default interface config at %s and exit\n"
        "  -h   Show this help\n"
        "  -d   With -s: do not kill existing wpa_supplicant/hostapd\n",
        argv0, ONET_CONFIG_DIR, ONET_DEFAULT_INT);
}

typedef int (*iface_fn)(const iface_config_t *, const global_config_t *);

static int iter_iface_configs(iface_fn fn, const global_config_t *g) {
    struct dirent **list = NULL;
    int n = scandir(ONET_CONFIG_DIR, &list, parse_ext, alphasort);
    if (n < 0) {
        perror(ONET_CONFIG_DIR);
        return -1;
    }
    if (n == 0) {
        printf("No .int interface configs in %s\n", ONET_CONFIG_DIR);
        free(list);
        return 0;
    }
    int rc = 0;
    for (int i = 0; i < n; i++) {
        char path[512];
        snprintf(path, sizeof path, "%s/%s",
                 ONET_CONFIG_DIR, list[i]->d_name);
        printf("\n--- %s ---\n", list[i]->d_name);

        iface_config_t iface;
        if (config_load_iface(path, &iface) < 0) {
            fprintf(stderr, "Failed to load %s, skipping\n", path);
            free(list[i]);
            continue;
        }
        if (fn(&iface, g) != 0) rc = -1;
        free(list[i]);
    }
    free(list);
    return rc;
}

int main(int argc, char *argv[]) {
    int s_flag = 0, d_flag = 0, w_flag = 0, h_flag = 0, g_flag = 0;
    int opt;
    while ((opt = getopt(argc, argv, "sdwhg")) != -1) {
        switch (opt) {
            case 's': s_flag = 1; break;
            case 'd': d_flag = 1; break;
            case 'w': w_flag = 1; break;
            case 'h': h_flag = 1; break;
            case 'g': g_flag = 1; break;
            default:  usage(argv[0]); return 2;
        }
    }
    if (argc <= 1 || h_flag) {
        usage(argv[0]);
        return h_flag ? 0 : 2;
    }
    if (s_flag + w_flag + g_flag > 1) {
        fprintf(stderr, "-s, -w, and -g are mutually exclusive\n");
        return 2;
    }
    if (geteuid() != 0) {
        fprintf(stderr, "ONeT must be run as root\n");
        return 1;
    }

    if (config_ensure_dirs() < 0) return 1;

    /* Bootstrap a global config the first time we run. */
    if (access(ONET_GLOBAL_INI, F_OK) != 0) {
        global_config_t defaults;
        config_default_global(&defaults);
        if (config_save_global(ONET_GLOBAL_INI, &defaults) < 0) {
            fprintf(stderr, "Failed to write %s\n", ONET_GLOBAL_INI);
            return 1;
        }
        printf("Created default %s — edit SSID/PSK before -s.\n",
            ONET_GLOBAL_INI);
    }

    if (g_flag) {
        iface_config_t iface;
        config_default_iface(&iface);
        if (config_save_iface(ONET_DEFAULT_INT, &iface) < 0) {
            fprintf(stderr, "Failed to write %s\n", ONET_DEFAULT_INT);
            return 1;
        }
        printf("Wrote %s\n", ONET_DEFAULT_INT);
        return 0;
    }

    global_config_t g;
    if (config_load_global(ONET_GLOBAL_INI, &g) < 0) {
        fprintf(stderr, "Failed to load %s\n", ONET_GLOBAL_INI);
        return 1;
    }

    if (s_flag) {
        if (!d_flag) hotspot_kill_existing();
        int rc = iter_iface_configs(hotspot_up, &g);
        if (rc == 0) dnsmasq_restart();
        return rc < 0 ? 1 : 0;
    }
    if (w_flag) {
        iter_iface_configs(hotspot_down, &g);
        dnsmasq_remove();
        dnsmasq_restart();
        proc_write_int("/proc/sys/net/ipv4/ip_forward", 0);
        return 0;
    }

    usage(argv[0]);
    return 2;
}
