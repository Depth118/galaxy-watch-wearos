#!/bin/sh
# Build the Broadcom libbt-vendor.so for the SM-R800's BCM43012 (uart5, /dev/ttySAC5).
# Source: AOSP hardware/broadcom/libbt + four system/bt headers, android11-release.
# Needs: curl, bash, base64, Android NDK r26d (NDK=... path to the NDK root).
set -e
cd "$(dirname "$0")"
NDK=${NDK:-/d/r800/ndk/android-ndk-r26d}
W=work; mkdir -p $W/src $W/bdroid
G=https://android.googlesource.com/platform
[ -f $W/src/Android.mk ] || curl -sSL "$G/hardware/broadcom/libbt/+archive/refs/heads/android11-release.tar.gz" | tar -xz -C $W/src
for f in hci/include/bt_vendor_lib.h hci/include/bt_hci_bdroid.h hci/include/userial.h \
         stack/include/bt_types.h device/include/esco_parameters.h include/hardware/bluetooth.h; do
    [ -f $W/bdroid/$f ] || { mkdir -p $W/bdroid/$(dirname $f); curl -sSL "$G/system/bt/+/refs/heads/android11-release/$f?format=TEXT" | base64 -d > $W/bdroid/$f; }
done
touch shim/bdroid_buildcfg.h
bash $W/src/gen-buildcfg.sh vnd_r800.txt shim/vnd_buildcfg.h
CC=$(ls $NDK/toolchains/llvm/prebuilt/*/bin/armv7a-linux-androideabi28-clang* | head -1)
"$CC" -shared -fPIC -O2 -Wall -Wno-switch -Wno-unused-function -Wno-unused-parameter \
    -Wno-unused-variable -Wno-unused-but-set-variable \
    -Ishim -I$W/src/include -I$W/bdroid/hci/include -I$W/bdroid/include \
    -I$W/bdroid/device/include -I$W/bdroid/stack/include -I$W/bdroid \
    -o libbt-vendor.so $W/src/src/*.c -llog
echo "built $(pwd)/libbt-vendor.so"
# Deploy: copy libbt-vendor.so, bt_vendor.conf and the Tizen firmware
# BCM43012B0_002.001.021.0153.0156.hcd (rootfs p18 /usr/lib/firmware) to
# /oem/r800dev/fw/bt/; android-helper.sh bind-mounts the library over the vendor one.
