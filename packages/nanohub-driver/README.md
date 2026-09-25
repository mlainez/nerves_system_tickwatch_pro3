# TicWatch Pro 3 nanohub port

The module source in `src/` is derived from
`drivers/staging/nanohub` at commit
`7afff8306c00f4cb1caee6e779c1ce280a6cdd55` of
`ONE-WearOS/android_kernel_mobvoi_rover` (the Mobvoi Android 4.9
downstream kernel). The original GPL-2.0 notices are retained.

The port updates the SPI, GPIO descriptor, IIO, wait queue, wakeup-source,
timekeeping, scheduler and driver-model APIs for Linux 7.1. It deliberately
does not expose the downstream firmware download, erase, lock or unlock
sysfs attributes. The hub's stock firmware is used in place and this package
does not contain proprietary firmware.

`nanohubctl` implements the small host-interface subset needed to enable and
decode the priority sensors. Mobvoi's stock sensor list identifies heart rate
as nanohub sensor ID 100; accelerometer and gyroscope use the standard IDs 1
and 2.

