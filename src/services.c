#include "services.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
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

    fprintf(f, "interface=%s\n",       iface->name);
    fprintf(f, "driver=nl80211\n");
    fprintf(f, "ssid=%s\n",            g->ssid);
    fprintf(f, "country_code=%s\n",    g->country);
    fprintf(f, "channel=%s\n",         iface->channel);
    fprintf(f, "ignore_broadcast_ssid=0\n");
    fprintf(f, "ht_capab=[HT40][SHORT-GI-20][DSSS_CCK-40]\n");
    fprintf(f, "ieee80211n=1\n");
    fprintf(f, "wmm_enabled=1\n");
    fprintf(f, "auth_algs=1\n");
    fprintf(f, "wpa=2\n");
    fprintf(f, "wpa_key_mgmt=WPA-PSK\n");
    fprintf(f, "rsn_pairwise=CCMP\n");
    fprintf(f, "wpa_passphrase=%s\n",  g->psk);

    if (iface->band == 1) {
        fprintf(f, "hw_mode=a\n");
        fprintf(f, "macaddr_acl=0\n");
        fprintf(f, "ieee80211d=1\n");
        fprintf(f, "ieee80211ac=1\n");
    } else {
        fprintf(f, "hw_mode=g\n");
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
