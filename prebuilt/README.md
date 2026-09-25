# Prebuilt boot images

Two files sit at the top of this repository. Both are flashed once, by
hand, before any Nerves firmware is installed, and neither is touched
again by `mix firmware` or by an over-the-air update.

| File | Flashed to | What it is |
| --- | --- | --- |
| `../lk2nd.img` | `boot` | Second-stage bootloader |
| `../dtbo.img` | `dtbo` | A deliberately empty device tree overlay |

## `dtbo.img`

The stock bootloader applies whatever device tree overlay it finds in the
`dtbo` partition on top of the device tree in the boot image. On this
watch that overlay is incompatible with lk2nd's, and applying it stops
lk2nd from running at all. Flashing something inert is the fix.

- Source: <https://github.com/NekoCWD/mobvoi-rover-dtbo> (release `First`)
- sha256: `10a2d1bed002355fe903d5c73f82df579a148ec9129ec78c772d3f7eddba9cd4`

This is the file the postmarketOS wiki points at for
[mobvoi-rover](https://wiki.postmarketos.org/wiki/Mobvoi_Ticwatch_Pro_3_LTE_(mobvoi-rover)#lk2nd).

It is worth knowing what it actually contains, because it is not simply
an empty overlay. The 254-byte file is a DTBO container declaring **two**
entries whose payloads sit at offsets 96 and 94837, each 94741 bytes long
— both of which run past the end of the file. Only the first 158 bytes of
the first payload are present:

```
/ {
	qcom,msm-id = <0x1a0 0x00>;   /* 416 — SDM429W, the LTE SoC */
	qcom,board-id = <0x10b 0x08>;
	__fixups__ { };
};
```

So it works by being truncated: there is nothing coherent for the
bootloader to apply. That is a blunt instrument, and it was tested on the
LTE watch. It should behave the same on the GPS watch — the file is
malformed before the SoC ID is ever compared — but that has not been
verified, and the only SoC ID it names is the LTE one.

### If it does not work on a GPS watch

`dtbo/make-dtbo.sh` builds a conventional alternative: a valid container
with one genuinely empty overlay per SoC, covering both 416 (SDM429W,
`rover`) and 437 (SDA429W, `rubyfish`).

```sh
./dtbo/make-dtbo.sh
fastboot flash dtbo dtbo/dtbo-empty.img
```

Try the shipped `dtbo.img` first — it is the one with real mileage on it.
Reach for this only if lk2nd does not come up.

## `lk2nd.img`

lk2nd release **23.1**, `msm8952` target, built from source — the
published 23.1 release image will **not** boot this watch, and neither
would a stock build of the current `main`.

The reason is how the stock bootloader picks a device tree. lk2nd ships
as an Android boot image with a table of appended DTBs, and the stock
bootloader selects the one whose `qcom,msm-id` and `qcom,board-id` match
the SoC it is running on. If nothing matches, it refuses to boot. Neither
of the TicWatch Pro 3's two IDs is in any released lk2nd.

Upstream [PR #558](https://github.com/msm8916-mainline/lk2nd/pull/558)
adds the LTE watch, and is still open. The GPS watch is not in that PR
either — Mobvoi's downstream kernel calls it `rubyfish` and it reports a
different SoC ID:

| Variant | Downstream codename | SoC | `qcom,msm-id` | `qcom,board-id` |
| --- | --- | --- | --- | --- |
| Pro 3 LTE | `rover` | SDM429W | 416 | `<0x10b 8>` |
| Pro 3 GPS | `rubyfish` | SDA429W | 437 | `<0x10b 8>` |

`SDM` has a modem, `SDA` does not; the boards are otherwise the same, and
`rubyfish.dts` downstream includes every `rover-*.dtsi` fragment and the
same 595 mAh battery profile. So `0001-dts-msm8952-add-Mobvoi-TicWatch-
Pro-3.patch` carries PR #558's file unmodified plus a second entry for
the GPS watch. Both point `lk2nd,dtb-files` at the one mainline device
tree, `sdm429w-mobvoi-rover`.

The image here has 29 appended DTBs — the 27 in release 23.1, plus these
two.

The patch is also committed as the
[`ticwatch-pro3`](https://github.com/Spin42/lk2nd/tree/ticwatch-pro3) branch
on Spin42's lk2nd fork, based on the 23.1 tag.

- sha256: `69abc759560e55602d3697b368a5f33c08507dc66745ab944396aa8595968ed6`
- Built with: `arm-none-eabi-gcc`, `LK2ND_VERSION=23.1-nerves-tickwatch-pro3`

### Rebuilding

```sh
./build-lk2nd.sh /tmp/lk2nd-work
```

It clones lk2nd at 23.1, applies the patch and builds the `msm8952`
target with a stripped environment (lk2nd is bare metal — distro CFLAGS
and ccache wrappers break it). The result is
`/tmp/lk2nd-work/lk2nd/build-lk2nd-msm8952/lk2nd.img`.

### If PR #558 lands upstream

Drop the `sdm429w-mobvoi-rover.dts` hunk from the patch and keep the
`rubyfish` one, which will still be needed for the GPS watch until it is
submitted separately.
