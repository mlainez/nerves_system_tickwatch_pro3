# Nerves System: Mobvoi TicWatch Pro 3

Nerves system for the **Mobvoi TicWatch Pro 3** — Qualcomm Snapdragon
Wear 4100, aarch64. One firmware image runs on both the GPS and the LTE
watch.

| Feature | Description |
| --- | --- |
| CPU | 4× Snapdragon Wear 4100 (Cortex-A53 @ 1.7 GHz, aarch64) |
| GPU | Adreno 504 — present, not used (see [Known gaps](#known-gaps)) |
| Memory | 1 GB LPDDR3 |
| Storage | 8 GB eMMC; firmware lives inside the stock `userdata` partition |
| Display | 454×454 AMOLED, via lk2nd's framebuffer and SimpleDRM |
| Touch | Zinitix BT541 |
| Linux | [`msm89x7-mainline/linux`](https://github.com/msm89x7-mainline/linux) 7.1.3-r1 |
| Bootloader | lk2nd (`msm8952` target) → `extlinux/extlinux.conf` on the boot subpartition |
| Console | On-device display (`tty1`), or telnet over USB from the initramfs |
| Wi-Fi / BT | Broadcom BCM43430A1 — `brcmfmac` over SDIO, `hci_uart` over UART |

## One image, two watches

The TicWatch Pro 3 ships in two variants, and they do not identify
themselves the same way to the bootloader:

| Variant | Downstream codename | SoC | `qcom,msm-id` |
| --- | --- | --- | --- |
| Pro 3 LTE | `rover` | SDM429W | 416 |
| Pro 3 GPS | `rubyfish` | SDA429W | 437 |

`SDM` has a modem; `SDA` does not. Beyond that they are the same board —
Mobvoi's downstream `rubyfish.dts` includes every one of `rover`'s device
tree fragments and the same 595 mAh battery profile — and mainline
describes both with a single `sdm429w-mobvoi-rover.dtb`.

The difference matters in exactly one place: the stock bootloader picks
which appended device tree to hand lk2nd by matching the SoC ID, so
`lk2nd.img` has to carry an entry for whichever watch you own. The image
in this repository carries both. See
[`prebuilt/README.md`](prebuilt/README.md).

## Status

Everything below is inherited from the mainline port; this system does
not change what the kernel supports.

Working: boot, eMMC, USB networking, display (SimpleDRM), touchscreen,
battery and charging, Wi-Fi, Bluetooth.

New and awaiting hardware validation: heart rate, accelerometer, gyroscope
and the stock-stack GPS compatibility path. Not working: audio, cellular
(LTE variant) and haptics. NFC enumerates but has no userspace support.

## Flashing

The stock bootloader will not chain-load a foreign kernel, so lk2nd goes
on `boot` and the Nerves firmware into `userdata`. Both `lk2nd.img` and
`dtbo.img` are in this repository and only need flashing once.

```sh
# Enter fastboot: power off, then hold the top button while plugging in USB.
fastboot flashing unlock          # once per watch — this erases it

fastboot flash dtbo  dtbo.img     # must come first; see below
fastboot flash boot  lk2nd.img
fastboot flash userdata my_app.img   # from `mix firmware.image`
fastboot reboot
```

**`dtbo.img` is not optional.** The stock bootloader applies the overlay
in the `dtbo` partition on top of lk2nd's device tree, and the stock
overlay stops lk2nd from running. The file here makes it apply nothing,
which is the documented fix. If lk2nd still does not come up on a GPS
watch, `prebuilt/dtbo/make-dtbo.sh` builds an alternative that names both
SoC IDs explicitly — see [`prebuilt/README.md`](prebuilt/README.md).

After that, `mix upload` works over the network as usual.

## Partition layout

The bootloader will not let us add eMMC partitions, so the whole Nerves
layout is nested *inside* the stock Android `userdata` partition. The
initramfs runs `kpartx -asf` on it very early to expose the pieces.

```
userdata   ── flashed with `fastboot flash userdata` ──
├─ MBR (block 0)
├─ uboot env       (Nerves firmware metadata, 8 KiB)
├─ Boot A          (ext2, 64 MiB — kernel + dtb + initramfs + extlinux)
├─ Boot B          (ext2, 64 MiB)
├─ Rootfs A        (squashfs, 384 MiB)
├─ Rootfs B        (squashfs, 384 MiB)
└─ Application     (f2fs, fills the rest, mounted at /root)
```

Nothing in this system names an eMMC partition number. `userdata` is not
at a fixed number across TicWatch Pro 3 firmware revisions, so
`extlinux.conf` passes `nerves_base=userdata` and the initramfs looks the
partition up by its GPT name, then publishes stable aliases that the rest
of the system uses:

| Alias | What |
| --- | --- |
| `/dev/rootdisk0` | standard Nerves alias for the stock `userdata` firmware container |
| `/dev/nerves-userdata` | the stock partition holding everything |
| `/dev/nerves-boot` | active boot slot (ext2, mounted at `/boot`, read-only) |
| `/dev/nerves-rootfs` | active rootfs slot (squashfs) |
| `/dev/nerves-app` | application data (f2fs, mounted at `/root`) |
| `/dev/nerves-vendor`, `/dev/nerves-persist` | stock Android partitions, read-only, for firmware |

`fwup` rewrites the nested MBR on every upgrade so that subpartitions 1,
2 and 3 always describe the slot that is about to run — which is why the
aliases never have to know which slot is live.

If you would rather pin the device explicitly, `rootfs=` and `bootpart=`
on the kernel command line still work and take precedence.

## Building

You need the Nerves toolchain prerequisites for your platform (see the
[Nerves installation guide](https://hexdocs.pm/nerves/installation.html)):
Erlang, Elixir, `fwup`, `squashfs-tools`, `cmake`, `autoconf`, `bc` and
`libssl-dev`.

```bash
mix archive.install hex nerves_bootstrap

# A throw-away app to build against.
mix nerves.new watch_demo --target ticwatch_pro3
cd watch_demo

export MIX_TARGET=ticwatch_pro3
mix deps.get      # pulls the toolchain (~250 MB), Buildroot and the kernel
mix firmware      # first build is 30–60 min; rebuilds are incremental
mix firmware.image # raw .img for fastboot
```

Useful side channels:

- `mix nerves.system.shell` — a shell in the Buildroot build directory
  with the environment set, for `make menuconfig` / `make linux-menuconfig`
- `MIX_DEBUG=1 mix compile` — every Buildroot command
- `rm -rf _build/ticwatch_pro3_* && mix deps.compile nerves_system_tickwatch_pro3 --force`
  — rebuild the system after a defconfig change

### Working on the kernel

`nerves_defconfig` pins the kernel to an exact commit so builds are
reproducible. To build from a local checkout instead, copy
`local.mk.example` to `local.mk` and point `LINUX_OVERRIDE_SRCDIR` at your
tree. `local.mk` is gitignored, and Buildroot `-include`s it, so its
absence is the normal case.

The kernel configuration is postmarketOS's
`config-postmarketos-qcom-msm89x7.aarch64` copied verbatim as
`linux-7.1.defconfig`, with this system's changes kept separately in
`linux-nerves.fragment`. Refreshing from pmaports is therefore a file
copy, and the fragment is a short, readable list of what Nerves needs
that postmarketOS does not.

The active 7.1.3-r1 kernel is pinned to
`50f9719b10cef792432485b0139fbcb913316e07`.
The CPU-removal patch is version-specific: 7.1 names the absent cores
`cpu0` through `cpu3`; the physical cores at 0x100 through 0x103 remain.

For rubyfish Wi-Fi, the board patch describes fixed 1.8-V SDIO I/O and
stock's 4-mA pin drive, retaining 50 MHz. It does not control unverified
external regulator GPIOs. A modprobe option disables only firmware WPA
authentication offload (`FWSUP`, bit 13), avoiding the missing authorization
notification expected by wpa_supplicant 2.11/2.12. Userspace WPA authentication
was verified after a firmware update over Wi-Fi, including a 120-ping test
with no packet loss; this is separate from SDIO power saving,
which remains enabled. No retry, extended-reset or diagnostic module is shipped.

When removing or changing kernel patches, re-extract the cached Linux source
before rebuilding: Buildroot does not undo patches already applied to it.

## Boot chain

```
stock bootloader  ── matches qcom,msm-id + qcom,board-id ──▶ lk2nd (boot partition)
lk2nd             ── scans partitions >16 MiB for /extlinux/extlinux.conf ──▶ boot subpartition
kernel + initramfs ── kpartx, mount squashfs, switch_root ──▶ erlinit ──▶ your OTP release
```

The second step is less obvious than it looks: the Nerves boot partition
is not an eMMC partition at all, it is nested inside `userdata`. lk2nd
copes because it publishes every GPT partition as a block device and then
runs its MBR parser over each one, so the nested layout shows up as
another level of block devices. It then mounts every leaf larger than
16 MiB as ext2 and looks for `/extlinux/extlinux.conf` on it — which our
64 MiB boot subpartition is, and has.

lk2nd gives the kernel, initramfs and device tree a combined **50 MiB**
of boot memory on this platform. That is the real constraint on the boot
partition, not its 64 MiB size — `scripts/create-boot-img.sh` warns if the
payload gets close. It is also why the initramfs carries no kernel
modules: everything on the path to `switch_root` is built into the kernel
instead (see `linux-nerves.fragment`).

## Driver bring-up

Nerves has no init system — `erlinit` is PID 1 and starts the BEAM, and
nothing else runs. There is no udev either, so nothing acts on the SDIO
and serdev uevents that would normally autoload a driver. Left alone this
watch boots to a working IEx prompt with no touchscreen, no Wi-Fi and no
Bluetooth.

`/usr/sbin/ticwatch-bringup` is the one place that ordering is expressed.
`erlinit` runs it through `--pre-run-exec`, which happens after the `-m`
mounts and before Erlang starts:

1. find the Wi-Fi NVRAM and the Bluetooth patch RAM image on the stock
   `/mnt/vendor` and `/mnt/persist` partitions
2. symlink them into a tmpfs under the names `brcmfmac` and `btbcm` ask
   for, and point `firmware_class.path` at it
3. `modprobe zinitix brcmfmac hci_uart`

The script does not wait for `wlan0` before starting Erlang. VintageNet
handles the interface appearing asynchronously. Applications should configure
a stable MAC through VintageNet; `hello_watch` derives it from the eMMC CID
with its `HelloWatch.WiFi.stable_mac/0` callback.

The order is the point. The drivers cannot just be built into the kernel:
the files they need live on a partition that is not mounted until erlinit
mounts it, long after a built-in driver would have probed and failed.
postmarketOS has the same constraint and solves it the same way, by
ordering `msm-firmware-loader` ahead of module loading.

The script always exits 0 — a missing blob costs you Wi-Fi, not a boot.
Check `dmesg` for lines prefixed `ticwatch-bringup:` to see what it found.

The initramfs names its `/dev` mount `devtmpfs`, matching erlinit's
shutdown exclusion. This avoids trying to unmount it while the console is
still open. Keep the mount and its transfer into the real root filesystem.
The package patch has been checked but still needs a firmware rebuild and
an update/reboot test.

### Sensor support

Rubyfish's motion and heart-rate sensors are behind an STM32 nanohub. The
`nanohub-driver` package ports Mobvoi's Android 4.9 SPI driver to Linux 7.1,
and the rover DT patch supplies the missing BLSP2 QUP4 controller and GPIO
wiring. Bringup loads the module without altering the hub's stock firmware;
the dangerous downstream flash/erase/lock sysfs controls are intentionally
not exposed.

Mobvoi's stock sensor table confirms IDs 100 (heart rate), 1 (accelerometer)
and 2 (gyroscope). Read hub metadata and stream newline-delimited JSON with:

```sh
nanohubctl info
nanohubctl stream heart
nanohubctl stream accel 50
nanohubctl stream gyro 50
```

These are direct hub events, not Android Sensor HAL output. On-device testing
after installing this firmware is still required, particularly for the
heart-rate nanoapp's measurement preconditions and calibration.

GPS is separate: the watch has a BCM4775 on BLSP1 SPI3. The
`bcm4775-driver` package ports its BBD transport and provides `/dev/ttyBCM`
and `/dev/bbd_*`. The `android-gps-compat` package mounts the watch's retained
Android `system` and `vendor` partitions read-only, starts the stock 32-bit
Broadcom `lhd`/`gpsd` stack on demand, and loads `gps.default.so` through a
small ARM32 bridge. No proprietary file is copied into this repository or the
firmware.

From a target shell, `gpsctl stream` prints newline-delimited JSON location,
satellite, NMEA and status events. `gpsctl stop`, `status` and `logs` manage
and diagnose the stack. In `hello_watch`, `HelloWatch.GPS.start_stream/0` and
`HelloWatch.GPS.subscribe/0` expose the same events as `{:gps, map}` messages.
GPS remains powered off until it is explicitly started. This path still needs
validation against the exact Mobvoi daemon/library revisions on a watch.

See the [downstream hub driver](https://github.com/ONE-WearOS/android_kernel_mobvoi_rover/tree/pie/drivers/staging/nanohub).

## Known gaps

- **Wi-Fi needs calibration data off your own watch.** The BCM43430A1's
  chip firmware ships in the rootfs, but its NVRAM does not — it carries
  the MAC address, so it is per-unit, and it lives on the stock Android
  partitions under the name the downstream `bcmdhd` driver used.
  `ticwatch-bringup` looks for it on the read-only `/mnt/vendor` and
  `/mnt/persist` mounts at boot and presents it under the name `brcmfmac`
  expects. If it finds nothing it says so in the log, and the interface
  will load but not come up. The same applies to the Bluetooth `.hcd`
  patch RAM image.
- **No GPU.** There is no KMS driver for this panel — mainline has no
  panel driver for it, so the device tree hands the kernel the
  framebuffer lk2nd set up and SimpleDRM binds it. That means no
  brightness control and no vsync, and nothing ever asks the Adreno 504
  for firmware. A graphics stack on top of this would go through Mesa's
  `kms_swrast` and render on the CPU.
- **Audio remains unsupported.** Heart rate, motion sensors and GPS now have
  kernel/userspace paths in the build, but require on-device validation;
  PM660 electrical and internal-temperature interfaces are also exposed.
- **No cellular** on the LTE watch. The modem is not brought up; the
  Qualcomm remoteproc/QRTR stack that `nerves_system_fp3` carries would
  be the starting point.
- **Time does not survive a reboot.** The PMIC RTC cannot be written.
  postmarketOS solves this with `swclock-offset`; on Nerves that belongs
  in an OTP application.

## Acknowledgements

The mainline port of this watch is [NekoCWD](https://github.com/NekoCWD)'s
work — the device tree, the lk2nd support in
[PR #558](https://github.com/msm8916-mainline/lk2nd/pull/558) and the
dummy dtbo. The kernel is [`msm89x7-mainline/linux`](https://github.com/msm89x7-mainline/linux),
maintained by Barnabas Czeman and others, and the packaging follows
postmarketOS's `device-qcom-msm89x7`.

The Nerves side follows [`nerves_system_fp3`](https://github.com/mlainez/nerves_system_fp3)
and [`nerves_system_fairphone2`](https://github.com/mlainez/nerves_system_fairphone2),
which is also where `qbootctl`, `reboot-mode` and `unudhcpd` come from.
The initramfs is the [Citronics](https://github.com/Citronics/initramfs)
one, patched to find its partitions by GPT name.

## License

Apache-2.0 — see [LICENSE](LICENSE). A built firmware image aggregates
many differently licensed components; see [NOTICE](NOTICE).
