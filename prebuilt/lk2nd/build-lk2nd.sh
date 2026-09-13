#!/bin/sh
#
# Rebuild lk2nd.img for the Mobvoi TicWatch Pro 3.
#
# Produces a byte-for-byte reproducible image given the same toolchain:
# lk2nd release 23.1 plus 0001-dts-msm8952-add-Mobvoi-TicWatch-Pro-3.patch.
#
# Needs: git, arm-none-eabi-gcc, dtc, python3.
#
# Usage:  ./build-lk2nd.sh [workdir]
# Writes: <workdir>/lk2nd/build-lk2nd-msm8952/lk2nd.img

set -e

LK2ND_TAG=23.1
LK2ND_REPO=https://github.com/msm8916-mainline/lk2nd.git
# Stamped into the image and shown on the lk2nd splash screen, so it is
# obvious on the watch that this is not a stock release build.
LK2ND_VERSION="${LK2ND_TAG}-nerves-tickwatch-pro3"

HERE=$(cd "$(dirname "$0")" && pwd)
WORKDIR=${1:-$(mktemp -d)}

mkdir -p "$WORKDIR"
cd "$WORKDIR"

if [ ! -d lk2nd ]; then
    git clone --recurse-submodules --branch "$LK2ND_TAG" "$LK2ND_REPO" lk2nd
fi
cd lk2nd

git checkout -q "$LK2ND_TAG"
git apply --check "$HERE/0001-dts-msm8952-add-Mobvoi-TicWatch-Pro-3.patch" 2>/dev/null &&
    git apply "$HERE/0001-dts-msm8952-add-Mobvoi-TicWatch-Pro-3.patch"

# lk2nd is a bare-metal build: distro CFLAGS, ccache and crossdirect
# wrappers all break it, so strip the environment down the way the
# postmarketOS APKBUILD does.
env -i PATH=/usr/sbin:/usr/bin:/sbin:/bin HOME="$HOME" \
    make LK2ND_VERSION="$LK2ND_VERSION" TOOLCHAIN_PREFIX=arm-none-eabi- lk2nd-msm8952

echo
echo "Built: $PWD/build-lk2nd-msm8952/lk2nd.img"
sha256sum build-lk2nd-msm8952/lk2nd.img
