#!/system/bin/sh
# Set up a Tizen rootfs chroot at /mnt/tz for Tizen's own sensor stack (lhd,
# sensord): rootfs (p18) read-only, Tizen's /opt (p16, system-data: vconf
# settings, dlog.conf, GNSS NV data) and /opt/usr (p17, mounted by Wear OS as /oem)
# in their real places, so Samsung's daemons see the configuration they expect.
B=/oem/r800dev/bin/busybox
T=/mnt/tz
mounted() { $B grep -q " $1 " /proc/mounts; }
$B mkdir -p $T
# tools/tzlite: the ~20 MB of the rootfs we actually use, kept on p17. p18 is
# Android's /data now; it is only used as the Tizen rootfs while it still is one
# (ext4 label "rootfs") and the subset is missing.
L=/oem/r800dev/tzlite
P18=/dev/block/mmcblk0p18
if [ -x $L/usr/bin/lhd ]; then
  mounted $T || { $B mount --bind $L $T && $B mount -o remount,bind,ro $T; }
elif [ "$($B dd if=$P18 bs=1 skip=1144 count=6 2>/dev/null)" = rootfs ]; then
  mounted $T || $B mount -o ro -t ext4 $P18 $T
else
  echo "no Tizen subset ($L) and p18 is not the Tizen rootfs"; exit 1
fi
for d in dev proc sys; do
  mounted $T/$d || $B mount --bind /$d $T/$d
done
mounted $T/dev/pts || $B mount --bind /dev/pts $T/dev/pts
for d in tmp run; do
  mounted $T/$d || $B mount -t tmpfs -o size=8m,mode=755 tmpfs $T/$d
done
mounted $T/opt || $B mount -t ext4 -o noatime /dev/block/mmcblk0p16 $T/opt
mounted $T/opt/usr || $B mount --bind /oem $T/opt/usr
$B mkdir -p $T/tmp/crash_stack
# kernel request_firmware() for /dev/bbd_patch runs in lhd's (chrooted) context,
# but ABOX/VTS firmware is requested from kernel workers that see Android's root.
# One path for both: /mnt/r800fw = /oem/r800dev/fw here (android-helper.sh) and
# in the chroot (its /mnt only holds two unused Tizen mount points).
mounted $T/mnt || $B mount -t tmpfs -o size=1m,mode=755 tmpfs $T/mnt
$B mkdir -p $T/mnt/r800fw
mounted $T/mnt/r800fw || $B mount --bind /oem/r800dev/fw $T/mnt/r800fw
$B mkdir -p /mnt/r800fw
mounted /mnt/r800fw || $B mount --bind /oem/r800dev/fw /mnt/r800fw
echo -n /mnt/r800fw > /sys/module/firmware_class/parameters/path
echo chroot-ready
