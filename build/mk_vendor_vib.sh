#!/bin/sh
# vendor_vib.img = vendor_omx.img (what the watch runs) + the stock vibrator HAL
# (android.hardware.vibrator@1.0 from the Android 9 donor) and its manifest entry.
set -e
cd /mnt/d/r800/test
cp vendor_omx.img vendor_vib.img
M=/mnt/vv; mkdir -p $M; mount -o loop vendor_vib.img $M
S=/mnt/d/r800/upstream/ztm620_motor
cp $S/android.hardware.vibrator@1.0-service $M/bin/hw/
cp $S/android.hardware.vibrator@1.0-service.rc $M/etc/init/
cp $S/android.hardware.vibrator@1.0-impl.so $S/vibrator.default.so $M/lib/hw/
chmod 755 $M/bin/hw/android.hardware.vibrator@1.0-service
chown 0:2000 $M/bin/hw/android.hardware.vibrator@1.0-service
chmod 644 $M/etc/init/android.hardware.vibrator@1.0-service.rc $M/lib/hw/android.hardware.vibrator@1.0-impl.so $M/lib/hw/vibrator.default.so
setfattr -n security.selinux -v u:object_r:hal_vibrator_default_exec:s0 $M/bin/hw/android.hardware.vibrator@1.0-service
setfattr -n security.selinux -v u:object_r:vendor_configs_file:s0 $M/etc/init/android.hardware.vibrator@1.0-service.rc
setfattr -n security.selinux -v u:object_r:vendor_file:s0 $M/lib/hw/android.hardware.vibrator@1.0-impl.so $M/lib/hw/vibrator.default.so
mkdir -p $M/usr/idc
cp /mnt/d/r800/upstream/detent/tizen_detent.idc $M/usr/idc/tizen_detent.idc
chmod 644 $M/usr/idc/tizen_detent.idc
setfattr -n security.selinux -v u:object_r:vendor_file:s0 $M/usr/idc/tizen_detent.idc
P=$M/etc/permissions/android.hardware.rotaryencoder.lowres.xml
cp /mnt/d/r800/upstream/detent/android.hardware.rotaryencoder.lowres.xml $P
chmod 644 $P; setfattr -n security.selinux -v u:object_r:vendor_configs_file:s0 $P
MF=$M/etc/vintf/manifest.xml
grep -q android.hardware.vibrator $MF || sed -i 's#</manifest>#    <hal format="hidl">\n        <name>android.hardware.vibrator</name>\n        <transport>hwbinder</transport>\n        <version>1.0</version>\n        <interface>\n            <name>IVibrator</name>\n            <instance>default</instance>\n        </interface>\n    </hal>\n</manifest>#' $MF
grep -c 'android.hardware.vibrator' $MF
ls -la $M/bin/hw | grep vibrator
# libhardware for the impl: VNDK 28 has it
umount $M
e2fsck -fn vendor_vib.img | tail -1
md5sum vendor_vib.img
