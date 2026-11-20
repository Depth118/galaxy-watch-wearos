#!/bin/sh
set -e
for t in as ld ar nm objcopy objdump strip readelf ranlib; do
    ln -sf /usr/bin/aarch64-alpine-linux-musl-$t /usr/bin/aarch64-linux-gnu-$t
done
ls -l /usr/bin/aarch64-linux-gnu-as
cd /root/kernel
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- CC=clang \
     LD=ld.lld AR=llvm-ar NM=llvm-nm OBJCOPY=llvm-objcopy OBJDUMP=llvm-objdump STRIP=llvm-strip \
     -j8 Image > /tmp/build5.log 2>&1 || true
echo "--- errors ---"
grep -n -i 'error' /tmp/build5.log | head -10
echo "--- tail ---"
tail -10 /tmp/build5.log
ls -l arch/arm64/boot/Image
