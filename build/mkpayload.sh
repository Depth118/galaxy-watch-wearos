set -e
rm -rf ~/payload && mkdir -p ~/payload/r800dev/bin ~/payload/r800dev/root
cp ~/aroot/bin/busybox.static ~/payload/r800dev/bin/busybox
cp -a ~/aroot/lib ~/aroot/sbin ~/payload/r800dev/root/
mkdir -p ~/payload/r800dev/root/usr && cp -a ~/aroot/usr/lib ~/aroot/usr/sbin ~/payload/r800dev/root/usr/
cd ~/payload && tar cf /mnt/d/r800/payload/r800dev.tar r800dev && ls -l /mnt/d/r800/payload/r800dev.tar
