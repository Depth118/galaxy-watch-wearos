set -e
which resize2fs >/dev/null || apk add -q e2fsprogs >/dev/null
cp /mnt/d/r800/donor/system_small.img /root/android/system_r800.img
mkdir -p /mnt/sys; mount -o loop /root/android/system_r800.img /mnt/sys
R=/mnt/sys/system
for a in priv-app/PrebuiltTapAndPayWearable priv-app/TicMcuService priv-app/ClockworkRetailAttractLoop priv-app/WristGesturesTutorial; do
  if [ -d $R/$a ]; then du -sk $R/$a | cut -f1 | xargs echo "remove $a KB:"; rm -rf $R/$a; else echo "MISSING $a"; fi
done
sync; umount /mnt/sys
e2fsck -fy /root/android/system_r800.img >/dev/null 2>&1 || true
e2fsck -fn /root/android/system_r800.img | tail -1
resize2fs -M /root/android/system_r800.img 2>&1 | tail -1
# a little slack so the image never runs completely full
BC=$(dumpe2fs -h /root/android/system_r800.img 2>/dev/null | awk '/^Block count/{print $3}')
resize2fs /root/android/system_r800.img $((BC + 2048)) 2>&1 | tail -1
e2fsck -fn /root/android/system_r800.img | tail -1
ls -l /root/android/system_r800.img
cp /root/android/system_r800.img /mnt/d/r800/donor/system_r800.img; md5sum /mnt/d/r800/donor/system_r800.img
