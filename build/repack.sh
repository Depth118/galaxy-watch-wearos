set -e
S=/root/android/system_r800.img; N=/root/android/system_r800_new.img
mkdir -p /mnt/sys /mnt/new
mountpoint -q /mnt/sys || mount -o loop,ro $S /mnt/sys
USED=$(df -k /mnt/sys | awk 'NR==2{print $3}')
SIZE_MB=$(( USED / 1024 * 104 / 100 + 48 ))
echo "used ${USED} KB -> new image ${SIZE_MB} MB"
rm -f $N; truncate -s ${SIZE_MB}M $N
mkfs.ext4 -q -b 4096 -I 256 -m 0 -O ^metadata_csum,^64bit,^orphan_file -L / $N
mount -o loop $N /mnt/new
tar --xattrs --xattrs-include='*' --numeric-owner -C /mnt/sys -cf - . | tar --xattrs --xattrs-include='*' --numeric-owner -C /mnt/new -xpf -
echo "files old $(find /mnt/sys | wc -l) new $(find /mnt/new | wc -l)"
echo "label old $(getfattr -n security.selinux --only-values /mnt/sys/system/bin/surfaceflinger) new $(getfattr -n security.selinux --only-values /mnt/new/system/bin/surfaceflinger)"
echo "init label: $(getfattr -n security.selinux --only-values /mnt/new/init 2>/dev/null) caps: $(getfattr -d -m security.capability /mnt/new/system/bin/* 2>/dev/null | grep -c capability)/$(getfattr -d -m security.capability /mnt/sys/system/bin/* 2>/dev/null | grep -c capability)"
getfattr -n security.selinux --only-values /mnt/sys/ ; echo; getfattr -n security.selinux --only-values /mnt/new/ 2>/dev/null || setfattr -n security.selinux -v "$(getfattr -n security.selinux --only-values /mnt/sys/)" /mnt/new/; echo
df -h /mnt/new | tail -1
umount /mnt/new; umount /mnt/sys
e2fsck -fn $N | tail -1
cp $N /mnt/d/r800/donor/system_r800.img; ls -l /mnt/d/r800/donor/system_r800.img; md5sum /mnt/d/r800/donor/system_r800.img
