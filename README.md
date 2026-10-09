# Nerves System: Mobvoi TicWatch Pro 3

Nerves system for the **Mobvoi TicWatch Pro 3** (Snapdragon Wear 4100,
aarch64). One firmware image runs on both the GPS and the LTE watch.

| Feature | Description |
| --- | --- |
| CPU | 4× Cortex-A53 @ 1.7 GHz |
| Memory / storage | 1 GB LPDDR3, 8 GB eMMC |
| Display | 454×454 AMOLED, MSM DRM with a native DSI panel driver |
| Touch | Zinitix BT541 |
| Wi-Fi / BT | Broadcom BCM43430A1 |
| Linux | [`msm89x7-mainline/linux`](https://github.com/msm89x7-mainline/linux) 7.1.3 + `patches/linux` |
| Bootloader | lk2nd → `extlinux.conf` on a boot subpartition |

**Working:** boot, eMMC, USB networking, display (with panel power-down),
touchscreen, battery and charging, Wi-Fi, Bluetooth.
**Needs validation:** heart rate, accelerometer, gyroscope, GPS.
**Not working:** audio, cellular, haptics, GPU.

## Quickstart

You need `fastboot` (macOS: `brew install android-platform-tools`;
Debian/Ubuntu: `apt install fastboot`), `curl` and `gzip`. `flash.sh` runs
on Linux and macOS.

Put the watch in fastboot mode: power it off, then hold the top button
while plugging in USB.

### Try the demo

```sh
git clone https://github.com/mlainez/nerves_system_tickwatch_pro3.git
nerves_system_tickwatch_pro3/flash.sh --unlock
```

This downloads the latest [`hello_watch`](https://github.com/mlainez/hello_watch)
image (a clock face with sensor pages) and flashes it with lk2nd.
`--unlock` unlocks the bootloader if needed, **which wipes the watch**. The
demo has no Wi-Fi or SSH configured.

### Your own app

You also need Elixir, `nerves_bootstrap` and `fwup`. Start from
`hello_watch`, or add the system to your app's `mix.exs`:

```elixir
{:nerves_system_tickwatch_pro3,
 github: "mlainez/nerves_system_tickwatch_pro3",
 tag: "v0.2.0",
 runtime: false,
 targets: :ticwatch_pro3}
```

```sh
export MIX_TARGET=ticwatch_pro3
mix deps.get            # downloads the prebuilt system
mix firmware.image      # writes <app>.img

deps/nerves_system_tickwatch_pro3/flash.sh --unlock my_app.img    # first install
deps/nerves_system_tickwatch_pro3/flash.sh --app-only my_app.img  # reinstall over USB
mix upload              # later updates over the network
```

`flash.sh` also takes `.fw`, `.img.gz`, `.img.xz` or an https URL; see
`--help`. It only writes `dtbo`, `boot` and `userdata`, so the stock
`vendor` and `persist` partitions (Wi-Fi and Bluetooth calibration) stay
intact.

## How it boots

The stock bootloader will not run a foreign kernel, so:

```
stock bootloader ──▶ lk2nd (boot partition)
lk2nd            ──▶ extlinux.conf on the Nerves boot subpartition
kernel+initramfs ──▶ kpartx, squashfs, switch_root ──▶ erlinit ──▶ your release
```

- **`dtbo.img` must be flashed** before lk2nd: the stock overlay in the
  `dtbo` partition stops lk2nd from running, and this one applies nothing.
- **Both watch variants** (LTE `rover`, SoC 416; GPS `rubyfish`, SoC 437)
  are in `lk2nd.img`; the stock bootloader picks lk2nd's device tree by
  SoC ID. See [`prebuilt/README.md`](prebuilt/README.md).
- lk2nd allows **50 MiB** for kernel, initramfs and DTB together;
  `scripts/create-boot-img.sh` warns when close. The initramfs carries no
  modules, so everything needed before `switch_root` is built in.

Flashing by hand, if you prefer:

```sh
fastboot flashing unlock              # once; wipes the watch
fastboot flash dtbo dtbo.img
fastboot flash boot lk2nd.img
fastboot flash userdata my_app.img
fastboot reboot
```

## Partition layout

The eMMC cannot be repartitioned, so the Nerves layout is nested inside
the stock `userdata` partition and exposed by `kpartx` in the initramfs:

```
userdata
├─ uboot env    (8 KiB)
├─ Boot A / B   (ext2, 64 MiB each)
├─ Rootfs A / B (squashfs, 384 MiB each)
└─ Application  (f2fs, rest, mounted at /root)
```

`userdata` is found by GPT name, not number. The initramfs publishes
stable aliases: `/dev/rootdisk0` and `/dev/nerves-userdata` (the
container), `/dev/nerves-boot`, `/dev/nerves-rootfs` and `/dev/nerves-app`
(active slot), and read-only `/dev/nerves-vendor` and
`/dev/nerves-persist`. `fwup` rewrites the nested MBR on upgrade so the
aliases always point at the slot about to run.

## Drivers and firmware

`erlinit` runs `/usr/sbin/ticwatch-bringup` before Erlang starts. It links
the per-unit Wi-Fi NVRAM and Bluetooth patch RAM from the stock
`/mnt/vendor` and `/mnt/persist` partitions into a tmpfs, points
`firmware_class.path` at it, then loads `zinitix`, `brcmfmac`, `hci_uart`
and `nanohub`. Look for `ticwatch-bringup:` in `dmesg`; a missing blob
costs Wi-Fi or Bluetooth, never the boot. Once Erlang is up,
`nerves_uevent` autoloads the remaining modules, including the display
drivers, which take over from SimpleDRM. Blanking `/dev/fb0` powers the
panel down, and brightness is under `/sys/class/backlight`.

Apps should set a stable Wi-Fi MAC through VintageNet; `hello_watch`
derives one from the eMMC CID.

**Sensors** sit behind an STM32 nanohub, driven by a port of Mobvoi's
driver that never touches the hub firmware:

```sh
nanohubctl info
nanohubctl stream heart        # also: accel 50, gyro 50
```

**GPS** (BCM4775) runs Mobvoi's own `lhd`/`gpsd` from the watch's
read-only `system` and `vendor` partitions; nothing proprietary is
shipped. `gpsctl stream` prints JSON events; `gpsctl stop|status|logs`
manage it. GPS stays off until started.

**Clock:** Linux cannot set the PMIC RTC. Apps keep time across reboots
by storing an offset, as `hello_watch`'s `HelloWatch.PmicRtc` does.

## Building the system

Only needed to change the system. Install the
[Nerves prerequisites](https://hexdocs.pm/nerves/installation.html), clone
this repository next to your app and depend on it with:

```elixir
{:nerves_system_tickwatch_pro3,
 path: "../nerves_system_tickwatch_pro3",
 runtime: false,
 targets: :ticwatch_pro3,
 nerves: [compile: true]}
```

The first `mix firmware` takes 30–60 minutes. `mix nerves.system.shell`
opens the Buildroot environment, and `mix nerves.artifact` packs the
tarball that releases publish.

The kernel is pinned to `msm89x7-mainline/linux@50f9719b` and patched
from `patches/linux`. Its config is postmarketOS's msm89x7 config
(`linux-7.1.defconfig`, unmodified) plus `linux-nerves.fragment`. To build
from a local kernel tree, copy `local.mk.example` to `local.mk`. After
changing patches, re-extract the kernel source: Buildroot does not undo
applied patches.

## Acknowledgements

The mainline port is [NekoCWD](https://github.com/NekoCWD)'s work: the
device tree, lk2nd support
([PR #558](https://github.com/msm8916-mainline/lk2nd/pull/558)) and the
dummy dtbo. The kernel is
[`msm89x7-mainline/linux`](https://github.com/msm89x7-mainline/linux),
maintained by Barnabas Czeman and others; packaging follows
postmarketOS's `device-qcom-msm89x7`. The Nerves side follows
[`nerves_system_fp3`](https://github.com/mlainez/nerves_system_fp3) and
[`nerves_system_fairphone2`](https://github.com/mlainez/nerves_system_fairphone2),
and the initramfs is [Citronics](https://github.com/Citronics/initramfs)'s.

## License

Apache-2.0, see [LICENSE](LICENSE). Firmware images aggregate components
under other licenses; see [NOTICE](NOTICE).
