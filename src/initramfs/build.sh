set -e
cp /mnt/d/r800/initramfs/init.c ~/initramfs/
cd ~/initramfs
clang --target=aarch64-linux-gnu -ffreestanding -nostdlib -static -fno-stack-protector -fno-builtin -O2 -fuse-ld=lld -Wl,-e,_start -o init init.c
ls -l init
cd ~/kernel
sh /mnt/d/r800/kbuild-native.sh Image 2>&1 | grep -E "error|OBJCOPY|copied|initramfs" | tail -6
md5sum arch/arm64/boot/Image
grep -c "mount p17 rw" arch/arm64/boot/Image; ls -l arch/arm64/boot/Image; cat .version
