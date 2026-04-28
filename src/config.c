#include "config.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "ezini.h"
#include "ini.h"

void config_default_global(global_config_t *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    snprintf(cfg->ssid,      sizeof cfg->ssid,      "%s", "ONeT-Hotspot");
    snprintf(cfg->psk,       sizeof cfg->psk,       "%s", "changeme1234");
    snprintf(cfg->fwd_iface, sizeof cfg->fwd_iface, "%s", "tun-easytether");
    snprintf(cfg->country,   sizeof cfg->country,   "%s", "US");
}

void config_default_iface(iface_config_t *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    snprintf(cfg->name,        sizeof cfg->name,        "%s", "eth0");
    snprintf(cfg->ip,          sizeof cfg->ip,          "%s", "192.168.2.1");
    snprintf(cfg->mask,        sizeof cfg->mask,        "%s", "255.255.255.0");
    snprintf(cfg->range_start, sizeof cfg->range_start, "%s", "192.168.2.2");
    snprintf(cfg->range_stop,  sizeof cfg->range_stop,  "%s", "192.168.2.254");
    snprintf(cfg->dns0,        sizeof cfg->dns0,        "%s", "8.8.8.8");
    snprintf(cfg->dns1,        sizeof cfg->dns1,        "%s", "8.8.4.4");
    snprintf(cfg->lease_time,  sizeof cfg->lease_time,  "%s", "12h");
    snprintf(cfg->channel,     sizeof cfg->channel,     "%s", "6");
    snprintf(cfg->phy_mode,    sizeof cfg->phy_mode,    "%s", "n");
    cfg->chwidth_mhz = 20;
    cfg->enabled = 0;
    cfg->band = 0;
}

static void copy_str(char *dst, size_t dstlen, const char *src) {
    if (!src) return;
    snprintf(dst, dstlen, "%s", src);
}

static int handler_global(void *user, const char *section,
                          const char *key, const char *value) {
    global_config_t *cfg = (global_config_t *)user;
    if (strcmp(section, "Hotspot") != 0) return 1;
    if      (strcmp(key, "ssid")      == 0) copy_str(cfg->ssid,      sizeof cfg->ssid,      value);
    else if (strcmp(key, "psk")       == 0) copy_str(cfg->psk,       sizeof cfg->psk,       value);
    else if (strcmp(key, "fwd_iface") == 0) copy_str(cfg->fwd_iface, sizeof cfg->fwd_iface, value);
    else if (strcmp(key, "country")   == 0) copy_str(cfg->country,   sizeof cfg->country,   value);
    return 1;
}

static int handler_iface(void *user, const char *section,
                         const char *key, const char *value) {
    iface_config_t *cfg = (iface_config_t *)user;
    if (strcmp(section, "Interface") != 0) return 1;
    if      (strcmp(key, "name")        == 0) copy_str(cfg->name,        sizeof cfg->name,        value);
    else if (strcmp(key, "ip")          == 0) copy_str(cfg->ip,          sizeof cfg->ip,          value);
    else if (strcmp(key, "mask")        == 0) copy_str(cfg->mask,        sizeof cfg->mask,        value);
    else if (strcmp(key, "range_start") == 0) copy_str(cfg->range_start, sizeof cfg->range_start, value);
    else if (strcmp(key, "range_stop")  == 0) copy_str(cfg->range_stop,  sizeof cfg->range_stop,  value);
    else if (strcmp(key, "dns0")        == 0) copy_str(cfg->dns0,        sizeof cfg->dns0,        value);
    else if (strcmp(key, "dns1")        == 0) copy_str(cfg->dns1,        sizeof cfg->dns1,        value);
    else if (strcmp(key, "lease_time")  == 0) copy_str(cfg->lease_time,  sizeof cfg->lease_time,  value);
    else if (strcmp(key, "channel")     == 0) copy_str(cfg->channel,     sizeof cfg->channel,     value);
    else if (strcmp(key, "phy_mode")    == 0) copy_str(cfg->phy_mode,    sizeof cfg->phy_mode,    value);
    else if (strcmp(key, "chwidth_mhz") == 0) cfg->chwidth_mhz = atoi(value);
    else if (strcmp(key, "enabled")     == 0) cfg->enabled = atoi(value) ? 1 : 0;
    else if (strcmp(key, "band")        == 0) cfg->band    = atoi(value);
    return 1;
}

int config_load_global(const char *path, global_config_t *cfg) {
    config_default_global(cfg);
    int rc = ini_parse(path, handler_global, cfg);
    return rc < 0 ? -1 : 0;
}

int config_load_iface(const char *path, iface_config_t *cfg) {
    config_default_iface(cfg);
    int rc = ini_parse(path, handler_iface, cfg);
    return rc < 0 ? -1 : 0;
}

int config_save_global(const char *path, const global_config_t *cfg) {
    ini_entry_list_t list = NULL;
    AddEntryToList(&list, "Hotspot", "ssid",      cfg->ssid);
    AddEntryToList(&list, "Hotspot", "psk",       cfg->psk);
    AddEntryToList(&list, "Hotspot", "fwd_iface", cfg->fwd_iface);
    AddEntryToList(&list, "Hotspot", "country",   cfg->country);
    int rc = MakeINIFile(path, list);
    FreeList(list);
    return rc == 0 ? 0 : -1;
}

int config_save_iface(const char *path, const iface_config_t *cfg) {
    char enabled[2] = { cfg->enabled ? '1' : '0', 0 };
    char band[4], chwidth[8];
    snprintf(band, sizeof band, "%d", cfg->band);
    snprintf(chwidth, sizeof chwidth, "%d", cfg->chwidth_mhz);

    ini_entry_list_t list = NULL;
    AddEntryToList(&list, "Interface", "name",        cfg->name);
    AddEntryToList(&list, "Interface", "ip",          cfg->ip);
    AddEntryToList(&list, "Interface", "mask",        cfg->mask);
    AddEntryToList(&list, "Interface", "range_start", cfg->range_start);
    AddEntryToList(&list, "Interface", "range_stop",  cfg->range_stop);
    AddEntryToList(&list, "Interface", "dns0",        cfg->dns0);
    AddEntryToList(&list, "Interface", "dns1",        cfg->dns1);
    AddEntryToList(&list, "Interface", "lease_time",  cfg->lease_time);
    AddEntryToList(&list, "Interface", "channel",     cfg->channel);
    AddEntryToList(&list, "Interface", "phy_mode",    cfg->phy_mode);
    AddEntryToList(&list, "Interface", "chwidth_mhz", chwidth);
    AddEntryToList(&list, "Interface", "enabled",     enabled);
    AddEntryToList(&list, "Interface", "band",        band);
    int rc = MakeINIFile(path, list);
    FreeList(list);
    return rc == 0 ? 0 : -1;
}

static int ensure_dir(const char *path, mode_t mode) {
    struct stat st;
    if (stat(path, &st) == 0) return 0;
    if (mkdir(path, mode) < 0 && errno != EEXIST) {
        perror(path);
        return -1;
    }
    return 0;
}

int config_ensure_dirs(void) {
    if (ensure_dir(ONET_DIR,         0700) < 0) return -1;
    if (ensure_dir(ONET_HOTSPOT_DIR, 0700) < 0) return -1;
    if (ensure_dir(ONET_CONFIG_DIR,  0700) < 0) return -1;
    if (ensure_dir(ONET_RUN_DIR,     0755) < 0) return -1;
    if (ensure_dir(ONET_LOG_DIR,     0755) < 0) return -1;
    return 0;
}
