#!/usr/bin/env bash
#
# Flash Nerves firmware onto a Mobvoi TicWatch Pro 3 over fastboot.
#
#   ./flash.sh                     # latest prebuilt hello_watch image
#   ./flash.sh my_app.img          # lk2nd + dtbo + firmware (first install)
#   ./flash.sh --app-only my_app.img
#   ./flash.sh my_app.fw           # converted to a raw image with fwup
#
# Run ./flash.sh --help for every option.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
RELEASE_URL="https://github.com/mlainez/nerves_system_tickwatch_pro3/releases/latest/download"
DEMO_URL="https://github.com/mlainez/hello_watch/releases/latest/download/hello_watch.img.gz"

LK2ND_SHA256=69abc759560e55602d3697b368a5f33c08507dc66745ab944396aa8595968ed6
DTBO_SHA256=10a2d1bed002355fe903d5c73f82df579a148ec9129ec78c772d3f7eddba9cd4

app_only=false
unlock=false
assume_yes=false
reboot=true
wait_seconds=60
firmware=""

usage() {
	cat <<EOF
Usage: $(basename "$0") [options] [firmware]

Flashes the TicWatch Pro 3 from fastboot mode. Enter it by powering the
watch off, then holding the top button while plugging in USB.

By default this writes, in order:
  dtbo      dtbo.img   (must precede lk2nd; see prebuilt/README.md)
  boot      lk2nd.img
  userdata  your firmware image — erases all data on the watch

Options:
  --app-only      Only flash userdata (lk2nd and dtbo are already installed)
  --unlock        Run 'fastboot flashing unlock' first if the bootloader is
                  locked. Confirm on the watch; this wipes it.
  --no-reboot     Stay in fastboot after flashing
  --wait SECONDS  How long to wait for the watch to appear (default: 60)
  -y, --yes       Do not ask for confirmation
  -h, --help      Show this help

firmware is a raw image from 'mix firmware.image' in your Nerves app, a
.img.gz or .img.xz of one, a .fw file (converted with fwup), or an https://
URL to any of these. Without it, the latest prebuilt hello_watch image is
downloaded from GitHub — a demo clock face with no Wi-Fi or SSH configured.

lk2nd.img and dtbo.img are taken from next to this script, or downloaded
from the latest GitHub release.
EOF
}

die() {
	echo "error: $*" >&2
	exit 1
}

info() {
	echo "==> $*"
}

while [ $# -gt 0 ]; do
	case "$1" in
	--app-only) app_only=true ;;
	--unlock) unlock=true ;;
	--no-reboot) reboot=false ;;
	--wait)
		[ $# -ge 2 ] || die "--wait needs a value"
		wait_seconds=$2
		shift
		;;
	-y | --yes) assume_yes=true ;;
	-h | --help)
		usage
		exit 0
		;;
	-*) die "unknown option: $1 (see --help)" ;;
	*)
		[ -z "$firmware" ] || die "only one firmware file can be given"
		firmware=$1
		;;
	esac
	shift
done

command -v fastboot >/dev/null || die "fastboot not found; install Android platform-tools"

sha256() {
	if command -v sha256sum >/dev/null; then
		sha256sum "$1" | cut -d' ' -f1
	else
		shasum -a 256 "$1" | cut -d' ' -f1
	fi
}

workdir=$(mktemp -d)
trap 'rm -rf "$workdir"' EXIT

# Finds a boot image next to this script, or downloads it, and checks it
# against the hash of the file this system was tested with.
boot_image() {
	local name=$1 expected=$2 path
	path="$SCRIPT_DIR/$name"
	if [ ! -f "$path" ]; then
		path="$workdir/$name"
		info "Downloading $name" >&2
		curl -fsSL -o "$path" "$RELEASE_URL/$name" ||
			die "could not download $name from $RELEASE_URL"
	fi
	[ "$(sha256 "$path")" = "$expected" ] ||
		die "$name does not match the expected sha256 $expected"
	echo "$path"
}

demo=false
if [ -z "$firmware" ]; then
	firmware=$DEMO_URL
	demo=true
	info "No firmware given; using the latest prebuilt hello_watch image"
fi

source_name=$(basename "${firmware%%\?*}")

case "$firmware" in
http://* | https://*)
	info "Downloading $firmware"
	local_firmware="$workdir/$source_name"
	curl -fL --progress-bar -o "$local_firmware" "$firmware" ||
		die "could not download $firmware"
	firmware=$local_firmware
	;;
*) [ -f "$firmware" ] || die "firmware file not found: $firmware" ;;
esac

case "$firmware" in
*.fw)
	command -v fwup >/dev/null || die "fwup is needed to convert a .fw file"
	info "Converting $firmware to a raw image"
	fwup -a -q -d "$workdir/firmware.img" -i "$firmware" -t complete
	image="$workdir/firmware.img"
	;;
*.gz)
	info "Decompressing $(basename "$firmware")"
	gzip -dc "$firmware" >"$workdir/firmware.img"
	image="$workdir/firmware.img"
	;;
*.xz)
	command -v xz >/dev/null || die "xz is needed to decompress $firmware"
	info "Decompressing $(basename "$firmware")"
	xz -dc "$firmware" >"$workdir/firmware.img"
	image="$workdir/firmware.img"
	;;
*) image=$firmware ;;
esac

if ! $app_only; then
	dtbo=$(boot_image dtbo.img "$DTBO_SHA256")
	lk2nd=$(boot_image lk2nd.img "$LK2ND_SHA256")
fi

info "Waiting for a fastboot device (power off, hold the top button, plug in USB)"
for ((i = 0; i < wait_seconds; i++)); do
	[ -n "$(fastboot devices 2>/dev/null)" ] && break
	sleep 1
done
devices=$(fastboot devices 2>/dev/null | awk 'NF' | wc -l)
[ "$devices" -ge 1 ] || die "no fastboot device found after ${wait_seconds}s"
[ "$devices" -eq 1 ] || die "more than one fastboot device connected; unplug the others"

getvar() {
	fastboot getvar "$1" 2>&1 | awk -F': ' -v k="$1" '$1 == k { print $2; exit }'
}

info "Found $(getvar product) (serial $(fastboot devices | awk 'NF { print $1; exit }'))"

if [ "$(getvar unlocked)" = "no" ]; then
	$unlock || die "the bootloader is locked; rerun with --unlock (this wipes the watch)"
	info "Unlocking the bootloader — confirm on the watch"
	fastboot flashing unlock
	info "Waiting for the watch to return to fastboot"
	for ((i = 0; i < wait_seconds; i++)); do
		[ "$(getvar unlocked)" = "yes" ] && break
		sleep 1
	done
	[ "$(getvar unlocked)" = "yes" ] || die "the bootloader is still locked"
fi

if ! $assume_yes; then
	if $app_only; then
		echo "This will overwrite userdata with $source_name, erasing everything in it."
	else
		echo "This will overwrite dtbo, boot (with lk2nd) and userdata with $source_name,"
		echo "erasing everything in userdata."
	fi
	read -r -p "Continue? [y/N] " answer
	case "$answer" in
	y | Y | yes | YES) ;;
	*) die "aborted" ;;
	esac
fi

if ! $app_only; then
	info "Flashing dtbo"
	fastboot flash dtbo "$dtbo"
	info "Flashing lk2nd to boot"
	fastboot flash boot "$lk2nd"
fi

info "Flashing firmware to userdata"
fastboot flash userdata "$image"

if $reboot; then
	info "Rebooting"
	fastboot reboot
fi

if $demo; then
	info "Done. The demo image has no Wi-Fi or SSH; build your own app for those."
else
	info "Done. Later updates can go over the network with 'mix upload'."
fi
