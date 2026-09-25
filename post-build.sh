#!/bin/sh

set -e

# Replace /data symlink (from skeleton) with a directory so GPS can bind-mount.
# The skeleton creates /data -> root, but android-gps-compat needs to bind-mount
# /root/android-gps-data to /data, which fails when /data is a symlink.
if [ -L "$TARGET_DIR/data" ]; then
    rm -f "$TARGET_DIR/data"
    mkdir -p "$TARGET_DIR/data"
fi

# android-gps-compat's client is an intentional 32-bit Bionic executable on
# this otherwise AArch64/glibc rootfs. The package installs it compressed so
# Buildroot's per-package ELF-architecture check ignores it; expand it only
# here, after target finalization, onto the executable squashfs.
GPS_BRIDGE_GZ=$TARGET_DIR/usr/libexec/gps-hal-bridge32.gz
if [ -f "$GPS_BRIDGE_GZ" ]; then
    gzip -dc "$GPS_BRIDGE_GZ" > $TARGET_DIR/usr/libexec/gps-hal-bridge32
    chmod 0755 $TARGET_DIR/usr/libexec/gps-hal-bridge32
    rm -f "$GPS_BRIDGE_GZ"
fi

# Build the fwup operations archive (status / factory-reset / revert /
# validate). Ends up on target at /usr/share/fwup/ops.fw.
mkdir -p $TARGET_DIR/usr/share/fwup
$HOST_DIR/usr/bin/fwup -c \
    -f $NERVES_DEFCONFIG_DIR/fwup-ops.conf \
    -o $TARGET_DIR/usr/share/fwup/ops.fw
ln -sf ops.fw $TARGET_DIR/usr/share/fwup/revert.fw

# Make the fwup includes resolvable next to the rootfs image.
cp -rf $NERVES_DEFCONFIG_DIR/fwup_include $BINARIES_DIR
