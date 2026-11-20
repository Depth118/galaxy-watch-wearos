#!/bin/sh
# Build the boot image (p11): kernel Image + device tree in Samsung's NZIT
# container.
#
#   build/mkboot.sh <Image> <src/dt/r800.dts> <template boot image> <out.img>
#
# The template supplies the DTB table header and the signature block (an
# existing boot image, e.g. images/boot/r800-boot.img). All six DTB table
# entries point at one 133120-byte DTB at offset 0x800. Run in WSL (dtc).
set -e
IMG=$1; DTS=$2; TPL=$3; OUT=$4
NZIT=$(dirname "$0")/../installer/nzit.py
T=$(mktemp -d)
python3 "$NZIT" unpack "$TPL" --out-dir $T/tpl > /dev/null
dtc -I dts -O dtb -S 133120 -o $T/r800.dtb "$DTS" 2> /dev/null
[ $(stat -c %s $T/r800.dtb) -eq 133120 ] || { echo "dtb is not 133120 bytes"; exit 1; }
cp $T/tpl/dtb.img $T/dtb.img
dd if=$T/r800.dtb of=$T/dtb.img bs=1 seek=2048 conv=notrunc 2> /dev/null
python3 "$NZIT" pack --kernel "$IMG" --dtb $T/dtb.img --sig $T/tpl/signature.bin \
    --pad-to 18874368 --out "$OUT" | tail -1
rm -rf $T
md5sum "$OUT"
