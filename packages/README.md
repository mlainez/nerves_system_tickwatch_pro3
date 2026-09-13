# Buildroot packages

Every package's `Config.in` title carries a scope tag indicating how
hardware-specific it is.

| Tag | Meaning |
|---|---|
| `[TicWatch Pro 3]` | Hardware-bound to this watch — its partition layout, its Broadcom chip, its device tree. Won't do anything useful on another board. |
| `[Qualcomm generic]` | Works on any Qualcomm SoC that ships the relevant kernel subsystem. |
| `[Generic]` | No hardware dependency. Could move upstream to `nerves_system_br`. |

## Current packages

| Package | Tag | Notes |
|---|---|---|
| `citronics-initramfs` | `[TicWatch Pro 3]` | Early-boot initramfs: maps the Nerves subpartitions out of the stock `userdata` partition and pivots into the squashfs. |
| `ticwatch-pro3-firmware` | `[TicWatch Pro 3]` | BCM43430A1 Wi-Fi/Bluetooth firmware plumbing. |
| `qbootctl` | `[Qualcomm generic]` | Userspace half of the bootloader's A/B slot accounting. |
| `reboot-mode` | `[Qualcomm generic]` | Reboots into `bootloader`/`recovery`/`edl`. |
| `unudhcpd` | `[Generic]` | Tiny single-IP DHCP server, used by the recovery initramfs. |

`qbootctl`, `reboot-mode` and `unudhcpd` are lifted unchanged from
[`nerves_system_fp3`](https://github.com/mlainez/nerves_system_fp3) —
they are board-neutral and there is no reason for three copies to drift.

## Where these came from

postmarketOS packages this watch through the generic
`device-qcom-msm89x7` device package rather than a device-specific one.
That package's dependency list is the starting point for what a Nerves
system needs, but most of it either does not apply to this watch or is
replaced by something Nerves already does:

| pmaports package | Here | Why |
|---|---|---|
| `linux-postmarketos-qcom-msm89x7` | `nerves_defconfig` + `linux-7.1.defconfig` + `linux-nerves.fragment` | Same fork and commit; Buildroot builds it. The config is pmOS's, verbatim, with our deltas in a fragment. |
| `lk2nd` (`lk2nd-msm8952`) | `../lk2nd.img` | A bootloader, not a rootfs package. Prebuilt and flashed by hand — and it needs a patch that is not upstream yet. See `../prebuilt/README.md`. |
| `device-qcom-msm89x7` | `extlinux/`, `fwup_include/`, `packages/citronics-initramfs/deviceinfo` | A pmOS device package is mostly deviceinfo plus a boot configuration. Those concepts exist here, just in Nerves' shapes. |
| `qbootctl` | `packages/qbootctl` | Ported. |
| `mkbootimg` | — | Host-side tool for building Android boot images. Nothing on the device needs it; `fwup` builds our images. |
| `postmarketos-base` | — | openrc services, `/etc` skeleton and a package manager. Nerves replaces all of it with erlinit and an OTP release. |
| `msm-firmware-loader` | `packages/ticwatch-pro3-firmware` | Dropped as such. It mounts the stock `modem`/`dsp`/`vendor`/`persist` partitions and points `firmware_class.path` at them, which is the right *idea* — but its file layout assumptions are Qualcomm-shaped, and this watch's Wi-Fi and Bluetooth are Broadcom. The much smaller `ticwatch-firmware-setup` does the same job for the two files that actually matter here. |
| `firmware-qcom-msm89x7` | — | WCNSS calibration for two Motorola phones. This watch has no WCNSS. |
| `firmware-qcom-adreno-a300` / `-a530` | — | The GPU is not used: there is no KMS driver for this panel, so the display is lk2nd's framebuffer via SimpleDRM and nothing ever asks Adreno for firmware. Add these when the GPU is brought up. |
| `soc-qcom-msm89x7` | — | Three things, none of which apply yet: a WirePlumber config (no PipeWire here), a GTK renderer workaround (no GTK), and ALSA UCM profiles (audio does not work on this device in mainline). The UCM half is worth porting when audio lands — it comes from `msm89x7-mainline/alsa-ucm-conf`. |
| `swclock-offset` | — | Keeps time across reboots on devices whose RTC cannot be written. Worth having, but it ships as an openrc service; on Nerves this belongs in an OTP application rather than a Buildroot package. |

## Adding a package

Pick the most restrictive tag that still applies. If it references this
watch's partition names, its Broadcom chip or its device tree, it is
`[TicWatch Pro 3]`. If it talks to a generic Qualcomm kernel subsystem,
it is `[Qualcomm generic]`. If it has no hardware assumption at all,
it is `[Generic]` — and consider whether it belongs upstream in
`nerves_system_br` instead of here.
