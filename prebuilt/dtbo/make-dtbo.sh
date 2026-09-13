#!/bin/sh
#
# Build a well-formed, do-nothing dtbo for the TicWatch Pro 3.
#
# Why this exists: the stock bootloader applies whatever overlay it finds
# in the `dtbo` partition on top of lk2nd's device tree, and the stock
# overlay stops lk2nd from running. The fix is to replace the partition
# with an overlay that changes nothing.
#
# The file normally used for this is NekoCWD's, which this repository
# ships as ../../dtbo.img. That one is known to work on the LTE watch.
# It is also deliberately malformed — its entry table points past the end
# of the 254-byte file — and the single complete overlay inside it
# declares qcom,msm-id 416 (SDM429W, the LTE SoC) only.
#
# This script builds a conventional alternative: a valid container with
# one genuinely empty overlay per SoC, covering both 416 (SDM429W, the
# LTE `rover`) and 437 (SDA429W, the GPS `rubyfish`). Use it only if the
# shipped dtbo.img does not get lk2nd running on your watch.
#
# Needs: dtc, python3.

set -e
cd "$(dirname "$0")"

OUT=${1:-dtbo-empty.img}
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

# 416 = SDM429W (TicWatch Pro 3 LTE, `rover`)
# 437 = SDA429W (TicWatch Pro 3 GPS, `rubyfish`)
# Both boards report the same qcom,board-id.
for id in 416 437; do
    cat > "$WORK/$id.dts" <<EOF
/dts-v1/;

/ {
	qcom,msm-id = <$id 0x00>;
	qcom,board-id = <0x10b 0x08>;

	__fixups__ {
	};
};
EOF
    dtc -I dts -O dtb -q -o "$WORK/$id.dtb" "$WORK/$id.dts"
done

python3 - "$WORK" "$OUT" <<'PY'
import struct, sys, os

work, out = sys.argv[1], sys.argv[2]
blobs = [open(os.path.join(work, f"{i}.dtb"), "rb").read() for i in (416, 437)]

HEADER_SIZE = 32
ENTRY_SIZE = 32
n = len(blobs)

# Payloads follow the entry table. Offsets are from the start of the file.
offset = HEADER_SIZE + ENTRY_SIZE * n
entries, payload = b"", b""
for b in blobs:
    # dt_size, dt_offset, id, rev, custom[4].
    #
    # id and rev are left at zero, matching the file this replaces: on
    # this platform the bootloader matches on the overlay's own
    # qcom,msm-id and qcom,board-id properties rather than on the table.
    entries += struct.pack(">8I", len(b), offset, 0, 0, 0, 0, 0, 0)
    payload += b
    offset += len(b)

total = HEADER_SIZE + ENTRY_SIZE * n + len(payload)
header = struct.pack(
    ">8I",
    0xD7B7AB1E,   # magic
    total,
    HEADER_SIZE,
    ENTRY_SIZE,
    n,
    HEADER_SIZE,  # dt_entries_offset
    2048,         # page_size
    0,            # version
)
open(out, "wb").write(header + entries + payload)
print(f"wrote {out}: {total} bytes, {n} overlays (msm-id 416 and 437)")
PY

sha256sum "$OUT"
