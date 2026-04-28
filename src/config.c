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
    snprintf(cfg->ssid,    sizeof cfg->ssid,    "%s", "ONeT-Hotspot");
    snprintf(cfg->psk,     sizeof cfg->psk,     "%s", "changeme1234");
    snprintf(cfg->country, sizeof cfg->country, "%s", "US");
    snprintf(cfg->fwd_iface, sizeof cfg->fwd_iface, "%s", "tun-easytether");

    snprintf(cfg->wan.name, sizeof cfg->wan.name, "%s", "tun-easytether");
    snprintf(cfg->wan.type, sizeof cfg->wan.type, "%s", "tether");
    cfg->wan.nat = -1;  /* sentinel: auto (off for tether, on otherwise) */
    snprintf(cfg->wan.watchdog_target, sizeof cfg->wan.watchdog_target, "%s", "1.1.1.1");
    cfg->wan.watchdog_interval_s = 30;
    cfg->wan.watchdog_failures = 3;
    snprintf(cfg->wan.qos, sizeof cfg->wan.qos, "%s", "fq_codel");

    cfg->fw.input_drop_wan = 1;
    cfg->v6.enable = 1;
    cfg->v6.pd = 0;
    cfg->v6.pd_length = 60;
    snprintf(cfg->v6.ula_prefix, sizeof cfg->v6.ula_prefix, "%s", "fd00:dead:beef");
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
    cfg->ipv6 = 1;
    /* bridge / bridge_members default empty */
}

static void copy_str(char *dst, size_t dstlen, const char *src) {
    if (!src) return;
    snprintf(dst, dstlen, "%s", src);
}

static int handler_global(void *user, const char *section,
                          const char *key, const char *value) {
    global_config_t *cfg = (global_config_t *)user;
    if (strcmp(section, "Hotspot") == 0) {
        if      (strcmp(key, "ssid")      == 0) copy_str(cfg->ssid,      sizeof cfg->ssid,      value);
        else if (strcmp(key, "psk")       == 0) copy_str(cfg->psk,       sizeof cfg->psk,       value);
        else if (strcmp(key, "country")   == 0) copy_str(cfg->country,   sizeof cfg->country,   value);
        else if (strcmp(key, "fwd_iface") == 0) copy_str(cfg->fwd_iface, sizeof cfg->fwd_iface, value);
    } else if (strcmp(section, "Wan") == 0) {
        if      (strcmp(key, "name")              == 0) copy_str(cfg->wan.name,            sizeof cfg->wan.name,            value);
        else if (strcmp(key, "type")              == 0) copy_str(cfg->wan.type,            sizeof cfg->wan.type,            value);
        else if (strcmp(key, "nat")               == 0) cfg->wan.nat = atoi(value) ? 1 : 0;
        else if (strcmp(key, "watchdog_target")   == 0) copy_str(cfg->wan.watchdog_target, sizeof cfg->wan.watchdog_target, value);
        else if (strcmp(key, "watchdog_interval") == 0) cfg->wan.watchdog_interval_s = atoi(value);
        else if (strcmp(key, "watchdog_failures") == 0) cfg->wan.watchdog_failures   = atoi(value);
        else if (strcmp(key, "qos")               == 0) copy_str(cfg->wan.qos,             sizeof cfg->wan.qos,             value);
        else if (strcmp(key, "modem_apn")         == 0) copy_str(cfg->wan.modem_apn,       sizeof cfg->wan.modem_apn,       value);
        else if (strcmp(key, "modem_index")       == 0) copy_str(cfg->wan.modem_index,     sizeof cfg->wan.modem_index,     value);
    } else if (strcmp(section, "Firewall") == 0) {
        if (strcmp(key, "input_drop_wan") == 0) cfg->fw.input_drop_wan = atoi(value) ? 1 : 0;
    } else if (strcmp(section, "IPv6") == 0) {
        if      (strcmp(key, "enable")     == 0) cfg->v6.enable = atoi(value) ? 1 : 0;
        else if (strcmp(key, "pd")         == 0) cfg->v6.pd     = atoi(value) ? 1 : 0;
        else if (strcmp(key, "pd_length")  == 0) cfg->v6.pd_length = atoi(value);
        else if (strcmp(key, "ula_prefix") == 0) copy_str(cfg->v6.ula_prefix, sizeof cfg->v6.ula_prefix, value);
    }
    return 1;
}

/* Resolve legacy fields and "auto" sentinels after load. */
static void resolve_global(global_config_t *cfg) {
    /* Legacy [Hotspot] fwd_iface → wan.name when [Wan] not specified. */
    if (cfg->wan.name[0] == '\0' && cfg->fwd_iface[0] != '\0') {
        snprintf(cfg->wan.name, sizeof cfg->wan.name, "%s", cfg->fwd_iface);
        if (cfg->wan.type[0] == '\0') {
            /* Old configs assumed an EasyTether-style tether. */
            snprintf(cfg->wan.type, sizeof cfg->wan.type, "%s", "tether");
        }
    }
    if (cfg->wan.type[0] == '\0') {
        snprintf(cfg->wan.type, sizeof cfg->wan.type, "%s", "ethernet");
    }
    /* nat=-1 sentinel: tether defaults off (Android masquerades), else on. */
    if (cfg->wan.nat == -1) {
        cfg->wan.nat = (strcmp(cfg->wan.type, "tether") == 0) ? 0 : 1;
    }
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
    else if (strcmp(key, "channel")        == 0) copy_str(cfg->channel,        sizeof cfg->channel,        value);
    else if (strcmp(key, "phy_mode")       == 0) copy_str(cfg->phy_mode,       sizeof cfg->phy_mode,       value);
    else if (strcmp(key, "bridge")         == 0) copy_str(cfg->bridge,         sizeof cfg->bridge,         value);
    else if (strcmp(key, "bridge_members") == 0) copy_str(cfg->bridge_members, sizeof cfg->bridge_members, value);
    else if (strcmp(key, "chwidth_mhz")    == 0) cfg->chwidth_mhz = atoi(value);
    else if (strcmp(key, "enabled")        == 0) cfg->enabled = atoi(value) ? 1 : 0;
    else if (strcmp(key, "band")           == 0) cfg->band    = atoi(value);
    else if (strcmp(key, "ipv6")           == 0) cfg->ipv6    = atoi(value) ? 1 : 0;
    return 1;
}

int config_load_global(const char *path, global_config_t *cfg) {
    config_default_global(cfg);
    /* Don't pre-fill wan.name from defaults; we want to detect "user didn't set it"
     * to apply the legacy fwd_iface fallback. */
    cfg->wan.name[0] = '\0';
    cfg->wan.type[0] = '\0';
    cfg->fwd_iface[0] = '\0';
    int rc = ini_parse(path, handler_global, cfg);
    if (rc < 0) return -1;
    resolve_global(cfg);
    return 0;
}

int config_load_iface(const char *path, iface_config_t *cfg) {
    config_default_iface(cfg);
    int rc = ini_parse(path, handler_iface, cfg);
    return rc < 0 ? -1 : 0;
}

int config_save_global(const char *path, const global_config_t *cfg) {
    char nat[2]    = { cfg->wan.nat > 0 ? '1' : '0', 0 };
    char drop[2]   = { cfg->fw.input_drop_wan ? '1' : '0', 0 };
    char v6en[2]   = { cfg->v6.enable ? '1' : '0', 0 };
    char wd_iv[8], wd_fl[8];
    snprintf(wd_iv, sizeof wd_iv, "%d", cfg->wan.watchdog_interval_s);
    snprintf(wd_fl, sizeof wd_fl, "%d", cfg->wan.watchdog_failures);

    ini_entry_list_t list = NULL;
    AddEntryToList(&list, "Hotspot", "ssid",    cfg->ssid);
    AddEntryToList(&list, "Hotspot", "psk",     cfg->psk);
    AddEntryToList(&list, "Hotspot", "country", cfg->country);

    AddEntryToList(&list, "Wan", "name",              cfg->wan.name);
    AddEntryToList(&list, "Wan", "type",              cfg->wan.type);
    AddEntryToList(&list, "Wan", "nat",               nat);
    AddEntryToList(&list, "Wan", "watchdog_target",   cfg->wan.watchdog_target);
    AddEntryToList(&list, "Wan", "watchdog_interval", wd_iv);
    AddEntryToList(&list, "Wan", "watchdog_failures", wd_fl);
    AddEntryToList(&list, "Wan", "qos",               cfg->wan.qos);
    AddEntryToList(&list, "Wan", "modem_apn",         cfg->wan.modem_apn);
    AddEntryToList(&list, "Wan", "modem_index",       cfg->wan.modem_index);

    AddEntryToList(&list, "Firewall", "input_drop_wan", drop);

    char pd[2]   = { cfg->v6.pd ? '1' : '0', 0 };
    char pd_l[8];
    snprintf(pd_l, sizeof pd_l, "%d", cfg->v6.pd_length);

    AddEntryToList(&list, "IPv6", "enable",     v6en);
    AddEntryToList(&list, "IPv6", "pd",         pd);
    AddEntryToList(&list, "IPv6", "pd_length",  pd_l);
    AddEntryToList(&list, "IPv6", "ula_prefix", cfg->v6.ula_prefix);

    int rc = MakeINIFile(path, list);
    FreeList(list);
    return rc == 0 ? 0 : -1;
}

int config_save_iface(const char *path, const iface_config_t *cfg) {
    char enabled[2] = { cfg->enabled ? '1' : '0', 0 };
    char ipv6[2]    = { cfg->ipv6 ? '1' : '0', 0 };
    char band[4], chwidth[8];
    snprintf(band, sizeof band, "%d", cfg->band);
    snprintf(chwidth, sizeof chwidth, "%d", cfg->chwidth_mhz);

    ini_entry_list_t list = NULL;
    AddEntryToList(&list, "Interface", "name",           cfg->name);
    AddEntryToList(&list, "Interface", "ip",             cfg->ip);
    AddEntryToList(&list, "Interface", "mask",           cfg->mask);
    AddEntryToList(&list, "Interface", "range_start",    cfg->range_start);
    AddEntryToList(&list, "Interface", "range_stop",     cfg->range_stop);
    AddEntryToList(&list, "Interface", "dns0",           cfg->dns0);
    AddEntryToList(&list, "Interface", "dns1",           cfg->dns1);
    AddEntryToList(&list, "Interface", "lease_time",     cfg->lease_time);
    AddEntryToList(&list, "Interface", "channel",        cfg->channel);
    AddEntryToList(&list, "Interface", "phy_mode",       cfg->phy_mode);
    AddEntryToList(&list, "Interface", "chwidth_mhz",    chwidth);
    AddEntryToList(&list, "Interface", "bridge",         cfg->bridge);
    AddEntryToList(&list, "Interface", "bridge_members", cfg->bridge_members);
    AddEntryToList(&list, "Interface", "enabled",        enabled);
    AddEntryToList(&list, "Interface", "band",           band);
    AddEntryToList(&list, "Interface", "ipv6",           ipv6);
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
