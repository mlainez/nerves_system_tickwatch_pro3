#!/bin/sh
# Rebuild gps-hal-bridge32 with any ARMv7 GCC-compatible cross compiler.
# The output is Bionic-linked through minimal link-only stubs; no Android NDK
# or proprietary library is used at build time.
set -eu

CC=${ARM_CC:-arm-none-linux-gnueabihf-gcc}
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
TMP=${TMPDIR:-/tmp}/gps-hal-bridge-build-$$
trap 'rm -rf "$TMP"' EXIT HUP INT TERM
mkdir -p "$TMP"

CFLAGS="-Os -fPIE -fno-stack-protector -fno-builtin -marm -march=armv7-a -mfpu=neon -mfloat-abi=softfp"
LDFLAGS="-nostdlib -pie -Wl,-e,_start -Wl,--dynamic-linker=/system/bin/linker -Wl,--no-as-needed"

"$CC" $CFLAGS -nostdlib -shared -Wl,-soname,libc.so \
	-o "$TMP/libc.so" "$HERE/android_stubs.c"
"$CC" $CFLAGS -nostdlib -shared -Wl,-soname,libdl.so \
	-o "$TMP/libdl.so" "$HERE/android_stubs.c"
"$CC" $CFLAGS -nostdlib -shared -Wl,-soname,gps.default.so \
	-o "$TMP/gps.default.so" "$HERE/android_gps_stub.c"
"$CC" $CFLAGS $LDFLAGS -L"$TMP" -o "$HERE/gps-hal-bridge32" \
	"$HERE/gps_hal_bridge.c" -Wl,--no-as-needed -l:gps.default.so -lc

echo "built $HERE/gps-hal-bridge32"
