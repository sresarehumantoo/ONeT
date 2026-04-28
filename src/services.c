#include "services.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "proc.h"

int dnsmasq_install(const iface_config_t *iface) {
    FILE *f = fopen(DNSMASQ_DROPIN, "a");
    if (!f) {
        perror(DNSMASQ_DROPIN);
        return -1;
    }
    fprintf(f, "interface=%s\n",      iface->name);
    fprintf(f, "bind-interfaces\n");
    fprintf(f, "server=%s\n",         iface->dns0);
    fprintf(f, "server=%s\n",         iface->dns1);
    fprintf(f, "domain-needed\n");
    fprintf(f, "bogus-priv\n");
    fprintf(f, "dhcp-range=%s,%s,%s\n",
        iface->range_start, iface->range_stop, iface->lease_time);
    fclose(f);
    return 0;
}

int dnsmasq_remove(void) {
    if (unlink(DNSMASQ_DROPIN) < 0 && errno != ENOENT) {
        perror(DNSMASQ_DROPIN);
        return -1;
    }
    return 0;
}

int dnsmasq_restart(void) {
    const char *argv[] = { "systemctl", "restart", "dnsmasq", NULL };
    return proc_run(argv);
}

/* hostapd's vht/he_oper_chwidth enum: 0 = 20/40, 1 = 80, 2 = 160, 3 = 80+80.
 * HE caps at 160; if EHT requests 320, HE still reports 160. */
static int chwidth_enum(int width_mhz) {
    if (width_mhz <= 40)  return 0;
    if (width_mhz == 80)  return 1;
    if (width_mhz >= 160) return 2;
    return 0;
}

/* eht_oper_chwidth adds 320 MHz (value 9). */
static int eht_chwidth_enum(int width_mhz) {
    if (width_mhz == 320) return 9;
    return chwidth_enum(width_mhz);
}

/* Center-frequency segment 0 index for a bonded primary on the lower edge.
 * Channel numbers are 5 MHz apart, channels are 20 MHz wide. */
static int center_seg0_idx(int primary, int width_mhz) {
    int n = width_mhz / 20;
    return primary + (n - 1) * 2;
}

/* Operating class for 6 GHz at the requested width. */
static int six_ghz_op_class(int width_mhz) {
    switch (width_mhz) {
        case 20:  return 131;
        case 40:  return 132;
        case 80:  return 133;
        case 160: return 134;
        case 320: return 137;
        default:  return 131;
    }
}

static int has_phy(const char *want, const char *mode) {
    /* ac, ax, be all imply n. ax, be imply ac (only valid on 5 GHz, caller filters).
     * be implies ax. */
    if (strcmp(want, "n") == 0)
        return strcmp(mode, "n") == 0 || strcmp(mode, "ac") == 0 ||
               strcmp(mode, "ax") == 0 || strcmp(mode, "be") == 0;
    if (strcmp(want, "ac") == 0)
        return strcmp(mode, "ac") == 0 || strcmp(mode, "ax") == 0 ||
               strcmp(mode, "be") == 0;
    if (strcmp(want, "ax") == 0)
        return strcmp(mode, "ax") == 0 || strcmp(mode, "be") == 0;
    if (strcmp(want, "be") == 0)
        return strcmp(mode, "be") == 0;
    return 0;
}

int hostapd_write_conf(const iface_config_t *iface,
                       const global_config_t *g,
                       char out_path[256]) {
    int n = snprintf(out_path, 256, "%s/hostapd-%s.conf",
                     ONET_RUN_DIR, iface->name);
    if (n < 0 || n >= 256) return -1;

    FILE *f = fopen(out_path, "w");
    if (!f) {
        perror(out_path);
        return -1;
    }
    /* Tighten perms before writing the PSK in plaintext. */
    fchmod(fileno(f), 0600);

    int primary = atoi(iface->channel);
    int width   = iface->chwidth_mhz > 0 ? iface->chwidth_mhz : 20;
    int center  = center_seg0_idx(primary, width);
    const char *mode = iface->phy_mode[0] ? iface->phy_mode : "n";

    int want_n  = has_phy("n",  mode);
    int want_ac = has_phy("ac", mode) && iface->band == 1;  /* 11ac is 5 GHz only */
    int want_ax = has_phy("ax", mode);
    int want_be = has_phy("be", mode);

    /* 6 GHz mandates WPA3-SAE; 11be also defaults to SAE for sanity. */
    int wpa3 = (iface->band == 2) || want_be;

    /* Base */
    fprintf(f, "interface=%s\n",   iface->name);
    fprintf(f, "driver=nl80211\n");
    fprintf(f, "ssid=%s\n",        g->ssid);
    fprintf(f, "country_code=%s\n", g->country);
    fprintf(f, "ieee80211d=1\n");
    if (iface->band == 1) fprintf(f, "ieee80211h=1\n");  /* DFS on 5 GHz */
    fprintf(f, "hw_mode=%s\n", iface->band == 0 ? "g" : "a");
    fprintf(f, "channel=%s\n", iface->channel);
    fprintf(f, "ignore_broadcast_ssid=0\n");
    fprintf(f, "wmm_enabled=1\n");
    fprintf(f, "auth_algs=1\n");
    fprintf(f, "macaddr_acl=0\n");

    if (iface->band == 2) {
        fprintf(f, "op_class=%d\n", six_ghz_op_class(width));
    }

    /* HT (802.11n) */
    if (want_n) {
        fprintf(f, "ieee80211n=1\n");
        if (width >= 40) {
            /* HT40+ assumes primary is the lower channel of the bonded pair.
             * Override in the generated conf if your primary is the upper. */
            fprintf(f, "ht_capab=[HT40+][SHORT-GI-20][SHORT-GI-40]\n");
        }
    }

    /* VHT (802.11ac, 5 GHz only) */
    if (want_ac) {
        fprintf(f, "ieee80211ac=1\n");
        fprintf(f, "vht_capab=[SHORT-GI-80][SHORT-GI-160][RX-LDPC]\n");
        fprintf(f, "vht_oper_chwidth=%d\n",            chwidth_enum(width));
        fprintf(f, "vht_oper_centr_freq_seg0_idx=%d\n", center);
    }

    /* HE (802.11ax / WiFi 6, 6E on band==2). Requires hostapd >= 2.10. */
    if (want_ax) {
        fprintf(f, "ieee80211ax=1\n");
        fprintf(f, "he_oper_chwidth=%d\n",            chwidth_enum(width));
        fprintf(f, "he_oper_centr_freq_seg0_idx=%d\n", center);
        fprintf(f, "he_su_beamformer=1\n");
        fprintf(f, "he_su_beamformee=1\n");
        fprintf(f, "he_mu_beamformer=1\n");
        fprintf(f, "he_default_pe_duration=4\n");
        fprintf(f, "he_rts_threshold=1023\n");
    }

    /* EHT (802.11be / WiFi 7). Requires hostapd >= 2.11. */
    if (want_be) {
        fprintf(f, "ieee80211be=1\n");
        fprintf(f, "eht_oper_chwidth=%d\n",            eht_chwidth_enum(width));
        fprintf(f, "eht_oper_centr_freq_seg0_idx=%d\n", center);
        fprintf(f, "eht_su_beamformer=1\n");
        fprintf(f, "eht_su_beamformee=1\n");
        fprintf(f, "eht_mu_beamformer=1\n");
    }

    /* Security */
    fprintf(f, "wpa=2\n");
    fprintf(f, "rsn_pairwise=CCMP\n");
    if (wpa3) {
        fprintf(f, "wpa_key_mgmt=SAE\n");
        fprintf(f, "ieee80211w=2\n");           /* PMF required */
        fprintf(f, "sae_require_mfp=1\n");
        fprintf(f, "sae_password=%s\n", g->psk);
    } else {
        fprintf(f, "wpa_key_mgmt=WPA-PSK\n");
        fprintf(f, "wpa_passphrase=%s\n", g->psk);
    }

    fclose(f);
    return 0;
}

static void hostapd_pidpath(const char *iface_name, char buf[256]) {
    snprintf(buf, 256, "%s/hostapd-%s.pid", ONET_RUN_DIR, iface_name);
}

static void hostapd_logpath(const char *iface_name, char buf[256]) {
    snprintf(buf, 256, "%s/hostapd-%s.log", ONET_LOG_DIR, iface_name);
}

int hostapd_start(const char *conf_path, const char *iface_name) {
    char pidfile[256], logfile[256];
    hostapd_pidpath(iface_name, pidfile);
    hostapd_logpath(iface_name, logfile);
    const char *argv[] = { "hostapd", conf_path, NULL };
    return proc_spawn_detached(argv, pidfile, logfile);
}

int hostapd_stop(const char *iface_name) {
    char pidfile[256];
    hostapd_pidpath(iface_name, pidfile);
    return proc_kill_pidfile(pidfile, SIGTERM);
}

int iptables_forward_install(const char *lan, const char *wan) {
    const char *a1[] = {
        "iptables", "-A", "FORWARD",
        "-i", wan, "-o", lan,
        "-m", "state", "--state", "RELATED,ESTABLISHED",
        "-j", "ACCEPT", NULL,
    };
    if (proc_run(a1) != 0) return -1;

    const char *a2[] = {
        "iptables", "-A", "FORWARD",
        "-i", lan, "-o", wan,
        "-j", "ACCEPT", NULL,
    };
    if (proc_run(a2) != 0) return -1;

    return 0;
}

int iptables_forward_remove(const char *lan, const char *wan) {
    /* Mirror of install. Failures here are expected when the rule
     * was never added, so silence them. */
    const char *a1[] = {
        "iptables", "-D", "FORWARD",
        "-i", wan, "-o", lan,
        "-m", "state", "--state", "RELATED,ESTABLISHED",
        "-j", "ACCEPT", NULL,
    };
    proc_run_quiet(a1);
    const char *a2[] = {
        "iptables", "-D", "FORWARD",
        "-i", lan, "-o", wan,
        "-j", "ACCEPT", NULL,
    };
    proc_run_quiet(a2);
    return 0;
}
