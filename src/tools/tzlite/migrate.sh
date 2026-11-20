# migrate.sh: copy Android /data (userdata.img on p17) onto p18 and grow it.
# Runs from p17 only; /data is frozen just for the block copy.
B=/oem/r800dev/bin/busybox
F=/oem/r800dev/bin/fsfreeze
SRC=/oem/r800dev/android/userdata.img
DST=/dev/block/mmcblk0p18
exec > /oem/r800dev/migrate.log 2>&1
echo "start $($B date)"
if $B grep -q mmcblk0p18 /proc/mounts; then echo "ABORT: p18 is mounted"; exit 1; fi
[ -b $DST ] && [ -f $SRC ] || { echo "ABORT: missing src/dst"; exit 1; }
sync
T0=$($B date +%s)
$F -f /data || { echo "ABORT: freeze failed"; exit 1; }
$B dd if=$SRC of=$DST bs=4M conv=fsync 2>&1; rc=$?
$F -u /data; urc=$?
echo "dd rc=$rc thaw rc=$urc frozen $(( $($B date +%s) - T0 )) s"
[ $rc = 0 ] || { echo "ABORT: dd failed"; exit 1; }
# (no image/p18 checksum: /data keeps writing to the image after the thaw;
#  e2fsck below checks the copy)
/system/bin/e2fsck -fy $DST; echo "e2fsck rc=$?"
/system/bin/resize2fs $DST; echo "resize2fs rc=$?"
/system/bin/tune2fs -L r800data $DST; echo "tune2fs rc=$?"
/system/bin/e2fsck -fn $DST; echo "e2fsck-check rc=$?"
echo "label=$($B dd if=$DST bs=1 skip=1144 count=8 2>/dev/null)"
echo "done $($B date)"
