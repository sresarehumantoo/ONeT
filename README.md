# ONeT Access

A small Linux routing/configuration tool that turns a Linux box into a Wi-Fi
or wired hotspot whose upstream is an Android phone running EasyTether
(`tun-easytether`). Designed to run on minimal embedded systems.

## Layout

```
src/                  Application code (one module per concern)
  main.c              Argv parsing and dispatch
  config.{c,h}        INI load/save, defaults, dir bootstrap
  netif.{c,h}         Interface presence + wireless detection (SIOCGIWNAME)
  proc.{c,h}          fork/execvp helpers, pidfile-based daemon control
  services.{c,h}      dnsmasq drop-in, hostapd config + spawn, iptables
  hotspot.{c,h}       Per-interface bring-up and tear-down orchestration
third_party/          Vendored libs (inih + ezini, unmodified)
Makefile              Builds build/onet
```

## Build

Requires `clang` (or `gcc`) and `make`.

```
make            # produces build/onet
make install    # installs to /usr/local/bin/onet (PREFIX overridable)
make clean
```

## Runtime requirements

- `hostapd`, `dnsmasq`, `iptables`, `ifconfig`, `ip`, `rfkill` on PATH
- Run as root (writes to `/etc/`, `/proc`, `/run`, `/var/log`)
- **hostapd ≥ 2.10** for WiFi 6 (`phy_mode=ax`)
- **hostapd ≥ 2.11** for WiFi 7 (`phy_mode=be`)
- Earlier hostapd versions work for `phy_mode=n` and `ac` (5 GHz)

## Usage

```
onet -s            Start the hotspot
onet -w            Wipe / tear down everything -s set up
onet -g            Generate /etc/ONeT/hotspot/config/default.int
onet -h            Help
onet -s -d         Start without killing existing wpa_supplicant/hostapd
```

`-s`, `-w`, and `-g` are mutually exclusive.

## Configuration

Files live under `/etc/ONeT/hotspot/`:

- `custom.ini` — global settings (created on first run with placeholders):

  ```ini
  [Hotspot]
  ssid=ONeT-Hotspot
  psk=changeme1234
  fwd_iface=tun-easytether
  country=US
  ```

- `config/*.int` — one file per LAN interface. Only files with `enabled=1`
  are brought up. Example (also what `-g` writes):

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
  phy_mode=n
  chwidth_mhz=20
  enabled=0
  band=0
  ```

### Wireless keys

| key           | values                          | meaning                                       |
|---------------|---------------------------------|-----------------------------------------------|
| `band`        | `0` / `1` / `2`                 | 2.4 GHz / 5 GHz / 6 GHz                       |
| `phy_mode`    | `n` / `ac` / `ax` / `be`        | WiFi 4 / 5 / 6 / 7                            |
| `chwidth_mhz` | `20` / `40` / `80` / `160` / `320` | Channel bandwidth                          |
| `channel`     | radio-specific                  | Primary channel (lowest of bonded set)        |

Constraints (hostapd will reject invalid combos):

- `phy_mode=ac` requires `band=1` (5 GHz only).
- `band=2` requires `phy_mode=ax` or `be`. 6 GHz mandates WPA3-SAE
  (PMF required), which `onet` selects automatically.
- `chwidth_mhz=320` requires `phy_mode=be` and `band=2`.
- `chwidth_mhz=160` is fine on 5 GHz `ac`/`ax`/`be` and on 6 GHz `ax`/`be`.

The HT40 secondary channel is emitted as `[HT40+]` (secondary above primary).
If your primary needs `[HT40-]`, edit the generated `/run/ONeT/hostapd-*.conf`
or pick a primary on the lower edge of your intended bonded group
(e.g. 36, 100, 149 on 5 GHz; 1, 33 on 6 GHz).

## Runtime artifacts

- `/run/ONeT/hostapd-<iface>.conf` — generated hostapd config (mode 0600)
- `/run/ONeT/hostapd-<iface>.pid`  — PID for `-w` to kill
- `/var/log/ONeT/hostapd-<iface>.log` — hostapd stdout/stderr
- `/etc/dnsmasq.d/onet.conf` — DHCP/DNS drop-in (removed by `-w`)

## Hardware that doesn't run like 2010

`onet` will happily emit a 320 MHz WiFi 7 6 GHz hostapd config. Whether
your radio actually delivers throughput is a separate question — and one
the userspace can't solve. Drivers and firmware are the bottleneck for
soft-AP on Linux; the chip matters far more than the SBC.

### Chips that work well in AP mode (in-tree drivers, real throughput)

- **MediaTek MT7916 / MT7986** (`mt76`) — 11ax 4×4 dual-band, 6 GHz
  capable. Mature mainline driver. Best price/perf for a Linux AP today.
- **MediaTek MT7996** (`mt76`) — 11be 4×4, 6 GHz with 320 MHz, MLO
  partial. The current "real WiFi 7 AP under Linux" baseline.
- **Qualcomm IPQ807x / WCN685x / QCN9074** (`ath11k`) — 11ax 4×4,
  5/6 GHz, AP mode supported. Common in commercial APs.
- **Qualcomm QCN9274 / IPQ957x** (`ath12k`) — 11be, 6 GHz 320 MHz.
  Driver matured during 2024; usable in 2025 kernels.
- **Atheros ath9k / ath10k** — fine for older 11n / 11ac APs if you don't
  need ax/be.

### Chips to avoid for AP mode

- **Broadcom anything with closed firmware** (`brcmfmac`) — Pi 4/5
  onboard radio (BCM43455/BCM43456), most consumer USB dongles.
  Soft-AP "works" up to 2.4 GHz `n` and slow 5 GHz `ac`, then throughput
  collapses. No 11ax in AP mode. No 6 GHz. Not a hostapd problem — it's
  the locked firmware. This was the wall I hit in high school.
- **Intel iwlwifi** (AX200/210, BE200) — designed for client mode.
  AP support is partial-to-broken depending on kernel. Don't rely on it.
- **Realtek USB dongles** (`rtl8821cu`, `rtl88x2bu`, similar) — usually
  out-of-tree drivers, AP mode brittle, MIMO/beamforming broken.
- **Anything sold on Amazon as "Linux WiFi adapter" without naming the
  chip.** The chip is what matters; the brand is theater.

### Reference platforms

- **BananaPi BPI-R4 + MT7996 NIC card** — 11be, 6 GHz 4×4, 320 MHz,
  fully supported by `mt76`. ~$120 total. The 2025 default if you want
  WiFi 7 on Linux.
- **BPI-R3 + MT7916** — 11ax 4×4 dual-band, ~$100. Solid budget WiFi 6
  AP.
- **Generic x86 mini-PC + MediaTek M.2 card** (MT7916 or MT7996) — most
  flexible, easy to expand later. Plenty of NUC-class boards in the
  $150–300 range with M.2 E-key for the radio.
- **Pi 5 + external Mediatek M.2 card via USB3-to-M.2 enclosure** —
  recovers the Pi from its onboard radio. Some throughput cost from USB,
  but vastly better than `brcmfmac`.

### The Pi reality check

The Raspberry Pi 4/5's onboard radio runs a 2.4 GHz hotspot fine and a
slow 5 GHz `ac` hotspot tolerably. That's the ceiling. No 11ax AP, no
6 GHz, no MU-MIMO, no real channel-bonding throughput. If your goal is
"hotspot to share a phone tether for one client at a time," the onboard
radio is fine. If your goal is "actually serve a few clients at modern
speeds," put a real radio on the M.2 / mPCIe slot or use a board built
around one (MT7986/MT7996 SBCs).

### And one honest disclaimer

The original ONeT context — sharing an Android USB tether via EasyTether
— means your WAN is whatever throughput the phone's radio negotiates and
the EasyTether tunnel can carry, which is usually well under 100 Mbps.
A WiFi 7 AP in front of a 50 Mbps tether is overkill. Use the same
toolchain on a real upstream (gigabit fiber, 5G CPE, etc.) and the
hardware choices above start to actually matter.
