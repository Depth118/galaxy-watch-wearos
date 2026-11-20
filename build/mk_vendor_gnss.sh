#!/bin/sh
# Install the R800 GNSS stack into a vendor image, in place:
#   bin/hw/vendor.samsung.hardware.gnss@1.0-service  (J4 / Android 9; registers
#        android.hardware.gnss IGnss from a passthrough impl, plus ISecGnss)
#   lib/vendor.samsung.hardware.gnss@1.0.so           (J4)
#   lib/hw/vendor.samsung.hardware.gnss@1.0-impl.so   (J4, ISecGnss)
#   lib/hw/android.hardware.gnss@1.0-impl.so          (AOSP legacy wrapper, from a
#        Samsung Android 8.1 dump; loads the legacy "gps" module)
#   lib/hw/gps.r800.so                                (hwc/gps_r800.c; talks to
#        tools/gps/gpsbridge in the Tizen chroot)
# The J4's own android.hardware.gnss@1.1-impl is Samsung's Kepler GNSS and is
# deliberately not installed, so the service falls back to the 1.0 impl.
#
#   build/mk_vendor_gnss.sh <vendor.img> <dir with the five files>
#
# Run as root (WSL, or the watch's Tizen with MNT=/tmp/vg). Re-running is harmless.
set -e
IMG=$1; D=$2
M=${MNT:-/mnt/vg}
mkdir -p $M
mount -o loop "$IMG" $M
trap 'umount $M' EXIT

put() {   # put <file> <dest dir> <mode> <selinux label>
    cp "$D/$1" "$M/$2/$1"
    chmod $3 "$M/$2/$1"
    setfattr -n security.selinux -v "u:object_r:$4:s0" "$M/$2/$1"
}
put vendor.samsung.hardware.gnss@1.0-service bin/hw 755 hal_gnss_default_exec
chown 0:2000 $M/bin/hw/vendor.samsung.hardware.gnss@1.0-service
put vendor.samsung.hardware.gnss@1.0.so lib 644 vendor_file
put vendor.samsung.hardware.gnss@1.0-impl.so lib/hw 644 vendor_file
put android.hardware.gnss@1.0-impl.so lib/hw 644 vendor_file
put gps.r800.so lib/hw 644 vendor_file

RC=$M/etc/init/vendor.samsung.hardware.gnss@1.0-service.rc
printf 'service sec_gnss_service /vendor/bin/hw/vendor.samsung.hardware.gnss@1.0-service\n    class hal\n    user system\n    group system gps\n' > $RC
chmod 644 $RC
setfattr -n security.selinux -v u:object_r:vendor_configs_file:s0 $RC

MF=$M/etc/vintf/manifest.xml
if ! grep -q 'android.hardware.gnss<' $MF; then
    sed -i 's#</manifest>#    <hal format="hidl">\n        <name>android.hardware.gnss</name>\n        <transport>hwbinder</transport>\n        <version>1.0</version>\n        <interface>\n            <name>IGnss</name>\n            <instance>default</instance>\n        </interface>\n    </hal>\n    <hal format="hidl">\n        <name>vendor.samsung.hardware.gnss</name>\n        <transport>hwbinder</transport>\n        <version>1.0</version>\n        <interface>\n            <name>ISecGnss</name>\n            <instance>default</instance>\n        </interface>\n    </hal>\n</manifest>#' $MF
fi
grep -c 'gnss' $MF
# the framework and Play look for this feature
P=$M/etc/permissions/android.hardware.location.gps.xml
printf '<?xml version="1.0" encoding="utf-8"?>\n<permissions>\n    <feature name="android.hardware.location.gps" />\n</permissions>\n' > $P
chmod 644 $P
setfattr -n security.selinux -v u:object_r:vendor_configs_file:s0 $P
ls -l $M/bin/hw/vendor.samsung.hardware.gnss@1.0-service $M/lib/hw/gps.r800.so $M/lib/hw/android.hardware.gnss@1.0-impl.so
sync
