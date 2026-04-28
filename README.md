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
  enabled=0
  band=0
  ```

  `band=0` → 2.4 GHz (`hw_mode=g`), `band=1` → 5 GHz (`hw_mode=a` + 11ac).

## Runtime artifacts

- `/run/ONeT/hostapd-<iface>.conf` — generated hostapd config (mode 0600)
- `/run/ONeT/hostapd-<iface>.pid`  — PID for `-w` to kill
- `/var/log/ONeT/hostapd-<iface>.log` — hostapd stdout/stderr
- `/etc/dnsmasq.d/onet.conf` — DHCP/DNS drop-in (removed by `-w`)
