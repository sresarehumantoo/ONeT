# ONeT Access

A small Linux routing/configuration tool that turns a Linux box into a
Wi-Fi (or wired/bridged) hotspot. Originally built to share an Android
phone tether (EasyTether) but now usable with a 5G/LTE modem
(`ModemManager`/`mmcli`), an upstream Ethernet, or any other interface.

## Layout

```
src/
  main.c           Argv parsing + dispatch (-s/-w/-g/-m/-h)
  config.{c,h}     INI load/save, schema, defaults, dir bootstrap
  log.{c,h}        Tiny logger (stderr or syslog)
  netif.{c,h}      Interface presence + wireless detection (SIOCGIWNAME)
  proc.{c,h}       fork/execvp helpers, pidfile-based daemon control
  services.{c,h}   dnsmasq/hostapd writers, iptables (NAT, INPUT, DNAT,
                   FORWARD, ip6tables), QoS (tc), static leases
  bridge.{c,h}     Linux bridge create/destroy + member enslave
  wan.{c,h}        WAN bring-up; for type=wwan invokes mmcli
  hotspot.{c,h}    Per-WAN and per-LAN orchestration
third_party/       Vendored libs (inih + ezini, untouched)
contrib/systemd/   Unit files for boot integration
Makefile           builds build/onet
```

## Build & install

Requires `clang` (or `gcc`) and `make`.

```sh
make                    # produces build/onet
sudo make install       # installs to /usr/local/bin/onet
sudo make install-systemd   # also installs the two service unit files
```

## Runtime requirements

- `hostapd`, `dnsmasq`, `iptables`, `ip6tables`, `ip`, `ifconfig`, `tc`,
  `ping`, `rfkill` on PATH.
- `mmcli` (from ModemManager) only when `[Wan].type=wwan`.
- Root, since ONeT writes to `/etc/`, `/proc`, `/run`, `/var/log` and
  configures the kernel networking stack.
- **hostapd ≥ 2.10** for `phy_mode=ax`; **≥ 2.11** for `phy_mode=be`.
  Earlier hostapd is fine for `n` and `ac`.

## Usage

```
onet -s          Start hotspot for every enabled .int
onet -w          Tear everything down
onet -g          Generate a default interface .int and exit
onet -m          Run watchdog (intended for systemd Type=simple)
onet -h          Help
onet -s -d       With -s: do not kill existing wpa_supplicant/hostapd
```

`-s`, `-w`, `-g`, and `-m` are mutually exclusive.

When run from a TTY, logs go to stderr with `[INFO]` / `[WARN]` /
`[ERROR]` prefixes. When stderr is not a TTY (under systemd, in a pipe),
logs go to syslog as `onet[<pid>]`.

## Configuration

Files live under `/etc/ONeT/hotspot/`:

```
custom.ini             Global config: SSID/PSK + [Wan]/[Firewall]/[IPv6]
config/*.int           Per-LAN interface configs
leases.conf            Optional static DHCP leases
port_forwards.conf     Optional port-forwarding rules
```

### `custom.ini` (global)

```ini
[Hotspot]
ssid=ONeT-Hotspot
psk=changeme1234
country=US
# Legacy alias — still works. Equivalent to [Wan].name with type=tether.
fwd_iface=tun-easytether

[Wan]
name=tun-easytether
type=tether                 ; tether | ethernet | wwan
nat=0                       ; default 0 for tether (Android masquerades),
                            ; default 1 for ethernet/wwan
watchdog_target=1.1.1.1
watchdog_interval=30
watchdog_failures=3
qos=fq_codel                ; fq_codel | cake | none
modem_apn=                  ; required when type=wwan
modem_index=                ; type=wwan only; "" → mmcli auto-detect

[Firewall]
input_drop_wan=1            ; drop new INPUT connections from WAN

[IPv6]
enable=1
pd=0                        ; 1 = request DHCPv6-PD on WAN via dhcpcd
pd_length=60                ; requested prefix length (carrier may give /60 or /56)
ula_prefix=fd00:dead:beef   ; used when pd=0 (LAN-only); /48; per-LAN /64 by hash
```

### Backward compatibility

The legacy `[Hotspot] fwd_iface=tun-easytether` from earlier versions
still works. If `[Wan]` is absent, ONeT promotes `fwd_iface` to
`[Wan].name` and assumes `type=tether` (which keeps NAT off — EasyTether
already masquerades on the Android side, so we don't double-NAT).

### `config/*.int` (per LAN)

```ini
[Interface]
name=eth0
ip=192.168.2.1
mask=255.255.255.0
range_start=192.168.2.2
range_stop=192.168.2.254
dns0=8.8.8.8
dns1=8.8.4.4
lease_time=12h
channel=6
phy_mode=n                  ; n | ac | ax | be
chwidth_mhz=20              ; 20 | 40 | 80 | 160 | 320
bridge=                     ; if set, this iface JOINS this bridge (hostapd bridge=)
bridge_members=             ; if set, this iface IS a bridge with these wired members
enabled=0
band=0                      ; 0 = 2.4 GHz, 1 = 5 GHz, 2 = 6 GHz
ipv6=1                      ; per-LAN IPv6 toggle
```

#### Wireless options

| key           | values                          | meaning                                  |
|---------------|---------------------------------|------------------------------------------|
| `band`        | `0` / `1` / `2`                 | 2.4 GHz / 5 GHz / 6 GHz                  |
| `phy_mode`    | `n` / `ac` / `ax` / `be`        | WiFi 4 / 5 / 6 / 7                       |
| `chwidth_mhz` | `20` / `40` / `80` / `160` / `320` | Channel bandwidth                     |
| `channel`     | radio-specific                  | Primary channel (lowest of bonded set)   |

Constraints (hostapd will reject invalid combos):
- `phy_mode=ac` requires `band=1` (5 GHz only).
- `band=2` requires `phy_mode=ax` or `be`. 6 GHz mandates WPA3-SAE,
  selected automatically.
- `chwidth_mhz=320` requires `phy_mode=be` and `band=2`.

The HT40 secondary is emitted as `[HT40+]` (secondary above primary).
If your primary needs `[HT40-]`, edit `/run/ONeT/hostapd-*.conf` after
bring-up, or pick a primary on the lower edge (36, 100, 149 in 5 GHz;
1, 33 in 6 GHz).

## Topologies

### EasyTether (legacy use-case, unchanged)

```ini
# custom.ini
[Hotspot]
ssid=Couch
psk=password1234
country=US
fwd_iface=tun-easytether
```
Plus one or more `.int` files for the wired/wireless LAN. NAT defaults
off because the Android EasyTether driver masquerades upstream.

### 5G/LTE modem via ModemManager

```ini
[Wan]
name=wwan0
type=wwan
modem_apn=internet
nat=1
qos=fq_codel
watchdog_target=1.1.1.1
```
On `-s`, ONeT runs `mmcli --enable` and `mmcli --simple-connect="apn=…"`
on the auto-detected modem (or the one you pin with `modem_index=`). On
`-w` it disconnects.

### Upstream Ethernet (one-armed router)

```ini
[Wan]
name=eth0
type=ethernet
nat=1
qos=fq_codel
```
Bring `eth0` up yourself first (DHCP via `dhclient`/systemd-networkd).
ONeT will MASQUERADE LAN traffic out of `eth0`.

### Bridged AP — eth1 + wlan0 on one /24

```ini
# config/br0.int   (the bridge owner)
[Interface]
name=br0
bridge_members=eth1
ip=192.168.2.1
mask=255.255.255.0
range_start=192.168.2.10
range_stop=192.168.2.250
dns0=8.8.8.8
dns1=8.8.4.4
lease_time=12h
enabled=1

# config/wlan0.int (the radio joins br0)
[Interface]
name=wlan0
bridge=br0
band=1
phy_mode=ax
chwidth_mhz=80
channel=36
enabled=1
```

The bridge-owner `.int` carries the L3 (IP, DHCP range, dnsmasq, iptables
FORWARD). The wireless `.int` only emits a hostapd config with
`bridge=br0`. ONeT processes bridge owners first, regardless of file
name, so `wlan0.int` can come alphabetically earlier than `br0.int`
without breaking ordering.

## Static DHCP leases

`/etc/ONeT/hotspot/leases.conf` (one per line, `#` comments OK):

```
# mac,ip,hostname
aa:bb:cc:dd:ee:ff,192.168.2.10,my-laptop
11:22:33:44:55:66,192.168.2.20,nas
```
Each valid line becomes a `dhcp-host=` directive in the dnsmasq drop-in.

## Port forwarding

`/etc/ONeT/hotspot/port_forwards.conf`:

```
# proto,wan_port,lan_ip,lan_port
tcp,8080,192.168.2.10,80
udp,5060,192.168.2.20,5060
```
Applied via `iptables -t nat PREROUTING DNAT` plus a matching
`FORWARD ACCEPT`. Removed cleanly on `-w`.

## IPv6

`enable=1` in `[IPv6]` plus `ipv6=1` per-`.int` does:

- Enables IPv6 forwarding (`/proc/sys/net/ipv6/conf/all/forwarding=1`).
- Assigns each LAN a /64 inside the configured ULA /48
  (e.g. `fd00:dead:beef:<hash>::1/64`). The subnet hash is a 16-bit
  FNV-1a of the interface name — stable across reboots.
- Emits dnsmasq `enable-ra` plus a `dhcp-range=::,constructor:<iface>,
  ra-only` for each LAN, so clients SLAAC into their /64.
- Adds an `ip6tables FORWARD` rule that allows ULA-to-ULA traffic so
  LANs can reach each other.

### IPv6 with prefix delegation (real internet)

Set `[IPv6] pd=1` and ONeT will:

1. Generate `/run/ONeT/dhcpcd.conf` listing the WAN interface plus every
   enabled LAN with `ipv6=1`, each numbered `/64` from the delegated block:

   ```
   interface wwan0
     ipv6rs
     ia_na 1
     ia_pd 1/::/60 br0/0/64 lan2/1/64 lan3/2/64
   ```
2. Spawn `dhcpcd -f /run/ONeT/dhcpcd.conf -B <wan>` detached, pidfile in
   `/run/ONeT/dhcpcd.pid`, log in `/var/log/ONeT/dhcpcd.log`.
3. Skip ULA address install on LANs (dhcpcd handles assignment).
4. Leave RA emission to dnsmasq (`dhcp-range=::,constructor:<iface>,
   ra-only`), so clients SLAAC into the delegated /64.

On `-w` (or watchdog recovery), dhcpcd is `SIGTERM`'d via the pidfile.

**Carriers vary**: AT&T/Verizon mostly hand out `/64` (no usable PD,
fall back to ULA), Comcast `/60`, T-Mobile Home Internet refuses PD.
Set `pd_length` to what your upstream actually delegates; if dhcpcd
doesn't acquire a prefix within ~30s, check `/var/log/ONeT/dhcpcd.log`.

## QoS

`[Wan].qos` controls the qdisc on the WAN egress:
- `fq_codel` (default) — bufferbloat mitigation, especially on
  cellular WANs.
- `cake` — better if the kernel/`tc` has it; pair with shaping params.
- `none` — leave the qdisc alone.

Implemented as `tc qdisc replace dev <wan> root <kind>`.

## Watchdog (-m)

Designed to run as a systemd `Type=simple` service. Pings
`[Wan].watchdog_target` every `watchdog_interval` seconds. After
`watchdog_failures` consecutive misses, runs a full
`hotspot_down → wan_down → wan_up → hotspot_up` cycle.

For `type=wwan`, this includes `mmcli --simple-disconnect` followed by
re-enabling and re-connecting the modem — useful for the ~daily flap
that 5G CPEs do.

## systemd integration

```sh
sudo make install-systemd
sudo systemctl daemon-reload
sudo systemctl enable --now onet onet-watchdog
```

`onet.service` is `Type=oneshot RemainAfterExit=yes` so `-w` runs on
stop. `onet-watchdog.service` is `Type=simple` and only starts after
`onet.service`.

## Runtime artifacts

- `/run/ONeT/hostapd-<iface>.conf`  — hostapd config (mode 0600)
- `/run/ONeT/hostapd-<iface>.pid`   — for `-w` / watchdog to terminate
- `/var/log/ONeT/hostapd-<iface>.log` — hostapd stdout/stderr
- `/etc/dnsmasq.d/onet.conf` — DHCP/DNS drop-in (rewritten on each `-s`)
- `iptables` chain `ONET_INPUT` — installed on `-s`, removed on `-w`

## Hardware that doesn't run like 2010

ONeT will happily emit a 320 MHz WiFi 7 6 GHz hostapd config. Whether
your radio actually delivers throughput is a separate question — and
one userspace can't solve.

### Chips that work well in AP mode

- **MediaTek MT7916 / MT7986** (`mt76`) — 11ax 4×4 dual-band, 6 GHz
  capable. Mature mainline. Best price/perf for a Linux AP.
- **MediaTek MT7996** (`mt76`) — 11be, 6 GHz, 320 MHz, MLO partial.
  The "real WiFi 7 AP under Linux" baseline as of 2025.
- **Qualcomm IPQ807x / WCN685x / QCN9074** (`ath11k`) — 11ax 4×4,
  5/6 GHz, AP mode supported. Common in commercial APs.
- **Qualcomm QCN9274 / IPQ957x** (`ath12k`) — 11be, 6 GHz 320 MHz.
  Driver matured during 2024.
- **Atheros ath9k / ath10k** — fine for older 11n / 11ac APs.

### Chips to avoid for AP mode

- **Broadcom anything with closed firmware** (`brcmfmac`) — Pi 4/5
  onboard radio, most consumer USB dongles. Soft-AP "works" up to slow
  5 GHz, then collapses. No 11ax AP. No 6 GHz. Locked firmware.
- **Intel iwlwifi** (AX200/210, BE200) — client-mode focus. AP support
  is partial-to-broken. Don't rely on it.
- **Realtek USB dongles** (`rtl8821cu`, `rtl88x2bu`, ...) — usually
  out-of-tree drivers, AP mode brittle, MIMO/beamforming broken.
- **Anything sold on Amazon as "Linux WiFi adapter" without naming
  the chip.** The chip is what matters.

### Reference platforms

- **BananaPi BPI-R4 + MT7996** — 11be, 6 GHz 4×4, 320 MHz.
  ~$120 total. The 2025 default for WiFi 7 on Linux.
- **BPI-R3 + MT7916** — 11ax 4×4 dual-band, ~$100.
- **x86 mini-PC + MT7916/MT7996 M.2 card** — most flexible.
- **Pi 5 + Mediatek M.2 card via USB3-to-M.2** — recovers the Pi from
  its onboard radio.

### 5G/LTE WAN reality

Cellular modems flap. Plan for it: leave the watchdog enabled,
prefer `qos=fq_codel` (cellular has horrible bufferbloat by default),
and don't expect public IPv6 unless your carrier actually delegates a
prefix (most don't on consumer plans — they NAT64 instead, which is a
problem for another tool).

For modem hardware: anything with a Quectel/Sierra Wireless module
(RM502Q-AE, EM7565, RM520N-GL) over USB3 or M.2 with `mbim` or `qmi`
mode is well-supported by `ModemManager`. RNDIS-only modems work but
the carrier IP lives behind another NAT layer, doubling NAT and
killing latency.
