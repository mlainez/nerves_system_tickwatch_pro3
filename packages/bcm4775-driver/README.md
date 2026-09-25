# TicWatch Pro 3 BCM4775 GNSS transport

The module source in `src/` is derived from `drivers/char/brcm/bbdpl` at
commit `7afff8306c00f4cb1caee6e779c1ce280a6cdd55` of
`ONE-WearOS/android_kernel_mobvoi_rover`. The GPL-2.0 notices are retained.

The port updates GPIO, timekeeping, driver-model and other kernel APIs for
Linux 7.1. It creates `/dev/ttyBCM`, `/dev/bbd_sensor`, `/dev/bbd_control`
and `/dev/bbd_patch`.

This is the kernel transport, not a complete GNSS stack. The stock Android
system feeds these devices to proprietary 32-bit Bionic `lhd` and `gpsd`
binaries. Those binaries are neither copied nor redistributed here, and are
not usable as native Nerves/aarch64-glibc programs. An open replacement for
the BCM4775 location-engine/BBD protocol is still required for fixes/NMEA.

