#!/bin/sh
# Build in WSL's native ext4 copy (~/kernel). Sync edited sources from D: first:
#   sh /mnt/d/r800/kbuild-native.sh sync    (copy changed files D: -> ~/kernel)
#   sh /mnt/d/r800/kbuild-native.sh Image   (build; copies Image back to D:)
set -e
SRC=/mnt/d/r800/kernel
DST=$HOME/kernel
MK="make ARCH=arm64 LLVM=1 LLVM_IAS=1 CC=clang LD=ld.lld AR=llvm-ar NM=llvm-nm OBJCOPY=llvm-objcopy OBJDUMP=llvm-objdump STRIP=llvm-strip CLANG_TRIPLE=aarch64-linux-gnu- CROSS_COMPILE=aarch64-linux-gnu- HOSTCC=clang -j$(nproc)"
if [ "$1" = "sync" ]; then
  cd "$SRC"
  # only source-ish files newer than the last sync stamp
  find . -newer "$DST/.last_sync" -type f \( -name '*.c' -o -name '*.h' -o -name '*.S' -o -name 'Makefile' -o -name 'Kconfig' -o -name '.config' -o -name '*.dts*' \) -print | while read f; do
    mkdir -p "$DST/$(dirname "$f")"; cp -p "$f" "$DST/$f"; echo "synced $f"; done
  touch "$DST/.last_sync"
  exit 0
fi
cd "$DST"
$MK "$@"
[ -f arch/arm64/boot/Image ] && cp arch/arm64/boot/Image "$SRC/arch/arm64/boot/Image" && echo "copied Image back to D:"
