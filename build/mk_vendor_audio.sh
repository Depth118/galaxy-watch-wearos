#!/bin/sh
# Install the R800 audio HAL into a vendor image, in place:
#   lib/hw/audio.primary.r800.so        (hwc/audio_r800.c, see tools/audio/README.md)
#   etc/audio_policy_configuration.xml  (vendor/audio/)
#   build.prop: ro.hardware.audio.primary=r800 (was the J4's universal7570, which
#     can't load: libsecnativefeature.so is missing), and A2DP offload off (the
#     Pixel system enables it; there is no offload HAL here).
#
#   build/mk_vendor_audio.sh <vendor.img> <audio.primary.r800.so> <audio_policy_configuration.xml>
#
# Run as root. Works on WSL and on the watch's Tizen (busybox-free: mount -o loop,
# setfattr). Re-running is harmless.
set -e
IMG=$1; SO=$2; XML=$3
M=${MNT:-/mnt/va}
mkdir -p $M
mount -o loop "$IMG" $M
trap 'umount $M' EXIT

cp "$SO" $M/lib/hw/audio.primary.r800.so
chmod 644 $M/lib/hw/audio.primary.r800.so
setfattr -n security.selinux -v u:object_r:vendor_file:s0 $M/lib/hw/audio.primary.r800.so

cp "$XML" $M/etc/audio_policy_configuration.xml
chmod 644 $M/etc/audio_policy_configuration.xml
setfattr -n security.selinux -v u:object_r:vendor_configs_file:s0 $M/etc/audio_policy_configuration.xml

XMLDIR=$(dirname "$XML")
for vf in audio_policy_volumes.xml default_volume_tables.xml; do
    if [ -f "$XMLDIR/$vf" ]; then
        cp "$XMLDIR/$vf" "$M/etc/$vf"
        chmod 644 "$M/etc/$vf"
        setfattr -n security.selinux -v u:object_r:vendor_configs_file:s0 "$M/etc/$vf"
    fi
done

P=$M/build.prop
sed -i 's/^ro.hardware.audio.primary=.*/ro.hardware.audio.primary=r800/' $P
grep -q '^ro.hardware.audio.primary=' $P || echo 'ro.hardware.audio.primary=r800' >> $P
if ! grep -q '^# r800 audio' $P; then
    printf '\n# r800 audio: no A2DP offload HAL; use the legacy a2dp module\nro.bluetooth.a2dp_offload.supported=false\npersist.bluetooth.a2dp_offload.disabled=true\n' >> $P
fi
grep -nE 'audio.primary|a2dp_offload' $P
ls -lZ $M/lib/hw/audio.primary.r800.so $M/etc/audio_policy_configuration.xml 2>/dev/null || ls -l $M/lib/hw/audio.primary.r800.so $M/etc/audio_policy_configuration.xml
sync
