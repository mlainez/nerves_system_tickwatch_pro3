#!/bin/sh
#
# create-boot-img.sh
#
# Builds the ext2 "boot" image that lk2nd reads at boot. Layout inside
# the image:
#
#   /Image                          — kernel
#   /sdm429w-mobvoi-rover.dtb       — device tree
#   /initramfs.gz                   — citronics initramfs (cpio.gz)
#   /extlinux/extlinux.conf         — bootloader configuration
#
# Inputs (env, from Buildroot's post-image stage):
#   $BINARIES_DIR         — buildroot output/images/
#   $NERVES_DEFCONFIG_DIR — this nerves_system_tickwatch_pro3 source tree
#
# Output:
#   $BINARIES_DIR/boot.img          — referenced from fwup.conf

set -e

STAGING="$BINARIES_DIR/boot"
rm -rf "$STAGING"
mkdir -p "$STAGING/extlinux"

cp "$NERVES_DEFCONFIG_DIR/extlinux/extlinux.conf" "$STAGING/extlinux/extlinux.conf"

# Kernel image. aarch64 builds drop an uncompressed `Image`.
for k in Image zImage; do
    if [ -f "$BINARIES_DIR/$k" ]; then
        cp "$BINARIES_DIR/$k" "$STAGING/$k"
    fi
done

# Device tree. One blob covers both the GPS (rubyfish) and LTE (rover)
# watches — see extlinux.conf.
for dtb in "$BINARIES_DIR"/sdm429w-mobvoi-*.dtb; do
    [ -f "$dtb" ] && cp "$dtb" "$STAGING/$(basename "$dtb")"
done

# citronics initramfs (built by the citronics-initramfs buildroot
# package; lands here as initramfs.gz).
if [ -f "$BINARIES_DIR/initramfs.gz" ]; then
    cp "$BINARIES_DIR/initramfs.gz" "$STAGING/initramfs.gz"
else
    echo "create-boot-img.sh: WARNING: no initramfs.gz in $BINARIES_DIR — booting without it will hang at rootfs mount." >&2
fi

# lk2nd loads the kernel, initramfs and DTB into a fixed 50 MiB window on
# this platform. Warn early rather than let the watch fail to boot with no
# console to read.
BOOT_BYTES=$(du -sb "$STAGING" | cut -f1)
BOOT_LIMIT=$((47 * 1024 * 1024))
if [ "$BOOT_BYTES" -gt "$BOOT_LIMIT" ]; then
    echo "create-boot-img.sh: WARNING: boot payload is $((BOOT_BYTES / 1024 / 1024)) MiB." >&2
    echo "  lk2nd gives kernel + initramfs + DTB a combined 50 MiB of boot memory" >&2
    echo "  (LK2ND_BOOT_MEM_SIZE). Past that the watch fails to boot silently." >&2
fi

# 1024-byte blocks * 65536 = 64 MiB; must stay <= BOOT_A_PART_COUNT
# (131072 blocks of 512 B each = 64 MiB) defined in fwup-common.conf.
genext2fs -B 1024 -b 65536 -d "$STAGING" "$BINARIES_DIR/boot.img"
