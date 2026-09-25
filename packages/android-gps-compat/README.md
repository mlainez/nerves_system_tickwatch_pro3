# Android GPS compatibility layer

The BCM4775 transport alone does not produce NMEA: Mobvoi's Broadcom location
engine and legacy Android GPS HAL implement the protocol above `/dev/ttyBCM`.
This package reuses those files in place from the watch's existing Android
`system` and `vendor` partitions. Both partitions remain read-only and no
Mobvoi/Broadcom binary is redistributed in the Nerves image.

`gpsctl stream` loads `bcm4775-bbd`, starts `gpsd`/`glgps` and `lhd`, then runs
the legacy HAL through the stock 32-bit Bionic linker. `gps-hal-bridge32`
converts GPS HAL callbacks to newline-delimited JSON on stdout.
`android-socket-exec` recreates the inherited `/dev/socket/gps` file descriptor
that Android init normally supplies to the Broadcom engine.

The HAL's `/data` state is stored on the normal writable Nerves application
partition. The Android `system` and `vendor` partitions are never written.

The checked-in bridge is a generated ARMv7 PIE because the main Nerves target
toolchain is AArch64-only. Rebuild it with:

```sh
ARM_CC=/path/to/arm-none-linux-gnueabihf-gcc ./build-prebuilt.sh
```

At runtime:

```sh
gpsctl status
gpsctl stream
gpsctl logs
gpsctl stop
```

The compatibility stack is opt-in and is never started during boot.
