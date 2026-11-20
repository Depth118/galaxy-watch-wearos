#!/bin/sh
# vendor_sens.img = <vendor.img> + the stock AOSP sensors HAL 1.0 service/impl (from
# the Android 9 donor vendor) + sensors.r800.so (SSP sensor hub, hwc/sensors_r800.c)
# + manifest entry and sensor feature declarations.
#
#   build/mk_vendor_sensors.sh <in vendor.img> <out vendor.img>     (run as root, WSL)
set -e
IN=$1
OUT=$2
S=/mnt/d/r800/upstream/sensors        # donor service/impl (Android 9 vendor); our .rc is vendor/sensors/ in the repo
MOD=/mnt/d/r800/hwc/sensors.r800.so
cp "$IN" "$OUT"
M=/mnt/vs; mkdir -p $M; mount -o loop "$OUT" $M

cp $S/android.hardware.sensors@1.0-service $M/bin/hw/
chmod 755 $M/bin/hw/android.hardware.sensors@1.0-service
chown 0:2000 $M/bin/hw/android.hardware.sensors@1.0-service
setfattr -n security.selinux -v u:object_r:hal_sensors_default_exec:s0 $M/bin/hw/android.hardware.sensors@1.0-service

cp $S/android.hardware.sensors@1.0-service.rc $M/etc/init/
chmod 644 $M/etc/init/android.hardware.sensors@1.0-service.rc
setfattr -n security.selinux -v u:object_r:vendor_configs_file:s0 $M/etc/init/android.hardware.sensors@1.0-service.rc

cp $S/android.hardware.sensors@1.0-impl.so $MOD $M/lib/hw/
chmod 644 $M/lib/hw/android.hardware.sensors@1.0-impl.so $M/lib/hw/sensors.r800.so
setfattr -n security.selinux -v u:object_r:vendor_file:s0 $M/lib/hw/android.hardware.sensors@1.0-impl.so $M/lib/hw/sensors.r800.so

# features the framework (and Play) look for; proximity from the donor is wrong here
feature() {
    P=$M/etc/permissions/$1.xml
    printf '<?xml version="1.0" encoding="utf-8"?>\n<permissions>\n    <feature name="%s" />\n</permissions>\n' $1 > $P
    chmod 644 $P; setfattr -n security.selinux -v u:object_r:vendor_configs_file:s0 $P
}
for f in accelerometer gyroscope barometer light heartrate stepcounter stepdetector; do
    feature android.hardware.sensor.$f
done
# low-latency off-body: Samsung's wear on/off (hub context, via sensord)
feature com.google.clockwork.hardware.sensor.llob
# features nothing provides (no proximity or ambient temperature sensor): apps
# must not expect them
for f in android.hardware.sensor.proximity android.hardware.sensor.ambient_temperature; do
    rm -f $M/etc/permissions/$f.xml
done

MF=$M/etc/vintf/manifest.xml
grep -q android.hardware.sensors $MF || sed -i 's#</manifest>#    <hal format="hidl">\n        <name>android.hardware.sensors</name>\n        <transport>hwbinder</transport>\n        <version>1.0</version>\n        <interface>\n            <name>ISensors</name>\n            <instance>default</instance>\n        </interface>\n    </hal>\n</manifest>#' $MF
grep -c 'android.hardware.sensors' $MF
ls -la $M/bin/hw | grep sensors
ls $M/etc/permissions | grep sensor
umount $M
e2fsck -fn "$OUT" | tail -1
md5sum "$OUT"
