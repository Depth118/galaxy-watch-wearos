#!/bin/sh
# Build gpsbridge for the Tizen rootfs (glibc 2.24, armv7 soft-float) with clang.
#   tools/gps/build.sh <dir with Tizen libc.so.6 libdl.so.2 libpthread.so.0> <out>
set -e
SR=$1
OUT=$2
cd "$(dirname "$0")"
clang --target=armv7a-linux-gnueabi -mfloat-abi=softfp -mfpu=vfpv3-d16 -O2 -fno-pic -no-pie \
    -fno-stack-protector -fno-builtin -ffreestanding -nostdlib -fuse-ld=lld \
    -Wl,--dynamic-linker=/lib/ld-linux.so.3 -Wl,-e,_start \
    -o "$OUT" gpsbridge.c "$SR/libc.so.6" "$SR/libdl.so.2" "$SR/libpthread.so.0"
ls -l "$OUT"
