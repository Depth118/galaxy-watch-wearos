set -e
apk add -q tar attr >/dev/null 2>&1 || true
SRC=/mnt/d/r800/donor/x/images/vendor.img
OUT=~/android/vendor_r800
umount /mnt/src 2>/dev/null || true
mount -o loop,ro $SRC /mnt/src
rm -rf $OUT; mkdir -p $OUT
cd /mnt/src
# keep: policy, vintf, permissions, props, uid tables, codecs; vndservicemanager; generic gralloc
tar --xattrs --xattrs-include='*' -cf - \
  ./etc/selinux ./etc/vintf ./etc/fs_config_dirs ./etc/fs_config_files ./etc/group ./etc/passwd \
  ./etc/seccomp_policy ./etc/permissions ./etc/media_codecs.xml ./etc/media_codecs_google_audio.xml \
  ./etc/media_codecs_google_telephony.xml ./etc/media_profiles_vendor.xml \
  ./etc/init/vndservicemanager.rc ./bin/vndservicemanager ./lib/hw/gralloc.default.so \
  ./default.prop ./build.prop ./lib/hw/android.hardware.graphics.mapper@2.0-impl.so \
  ./lib/hw/android.hardware.graphics.allocator@2.0-impl.so \n  ./bin/hw/android.hardware.configstore@1.1-service ./etc/init/android.hardware.configstore@1.1-service.rc ./lib/libhwminijail.so \n  ./bin/hw/android.hardware.power@1.0-service ./etc/init/android.hardware.power@1.0-service.rc \n  ./lib/hw/android.hardware.power@1.0-impl.so ./lib/hw/power.default.so \n  ./bin/hw/android.hardware.health@2.0-service ./etc/init/android.hardware.health@2.0-service.rc \n  ./bin/hw/android.hardware.graphics.allocator@2.0-service ./etc/init/android.hardware.graphics.allocator@2.0-service.rc \n  ./etc/seccomp_policy 2>/dev/null | tar --xattrs --xattrs-include='*' -C $OUT -xpf -
# vndservicemanager deps that live in vendor/lib
for lib in $(llvm-readelf -d bin/vndservicemanager 2>/dev/null | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p'); do
  [ -f lib/$lib ] && { mkdir -p $OUT/lib; tar --xattrs --xattrs-include='*' -cf - ./lib/$lib | tar --xattrs --xattrs-include='*' -C $OUT -xpf -; echo "dep: $lib (vendor)"; } || echo "dep: $lib (system/vndk)"
done
cd /
umount /mnt/src
# ---- J4 (SM-J400F, Android 9) graphics: Mali r28p0 + Samsung gralloc + composer service (fb adapter path) ----
mkdir -p /mnt/j4; umount /mnt/j4 2>/dev/null || true
mount -o loop,ro /root/android/j4_vendor.img /mnt/j4
rm -f $OUT/lib/hw/gralloc.default.so $OUT/lib/hw/android.hardware.graphics.allocator@2.0-impl.so $OUT/lib/hw/android.hardware.graphics.mapper@2.0-impl.so       $OUT/bin/hw/android.hardware.graphics.allocator@2.0-service $OUT/etc/init/android.hardware.graphics.allocator@2.0-service.rc
( cd /mnt/j4 && tar --xattrs --xattrs-include='*' -cf -   ./lib/egl/libGLES_mali.so ./lib/hw/gralloc.exynos5.so   ./lib/hw/android.hardware.graphics.allocator@2.0-impl.so ./lib/hw/android.hardware.graphics.mapper@2.0-impl.so   ./lib/hw/android.hardware.graphics.composer@2.1-impl.so ./lib/libhwc2on1adapter.so ./lib/libhwc2onfbadapter.so   ./lib/libion_exynos.so ./lib/hw/memtrack.universal7570.so ./lib/hw/android.hardware.memtrack@1.0-impl.so   ./bin/hw/android.hardware.graphics.allocator@2.0-service ./bin/hw/android.hardware.graphics.composer@2.1-service   ./bin/hw/android.hardware.memtrack@1.0-service   ./etc/init/android.hardware.graphics.allocator@2.0-service.rc ./etc/init/android.hardware.graphics.composer@2.1-service.rc   ./etc/init/android.hardware.memtrack@1.0-service.rc ) | tar --xattrs --xattrs-include='*' -C $OUT -xpf -
# ---- keymaster 3.0 (gatekeeper omitted: J4 gatekeeper.exynos7570.so crashes; gatekeeperd falls back to SoftGateKeeperDevice) ----
for f in \
    ./bin/hw/android.hardware.keymaster@3.0-service \
    ./etc/init/android.hardware.keymaster@3.0-service.rc \
    ./lib/hw/android.hardware.keymaster@3.0-impl.so \
    ./lib/android.hardware.keymaster@3.0.so \
    ./lib/libsoftkeymasterdevice.so \
    ./lib/libkeymaster_portable.so \
    ./lib/libpuresoftkeymasterdevice.so \
    ./lib/libkeymaster3device.so \
    ./lib/libskeymaster3device.so ; do
    if [ -e "/mnt/j4/$f" ]; then
        tar --xattrs --xattrs-include='*' -C /mnt/j4 -cf - "$f" | tar --xattrs --xattrs-include='*' -C $OUT -xpf -
        echo "km: $f"
    else
        echo "km: MISSING $f"
    fi
done
if [ -e "$OUT/etc/init/android.hardware.keymaster@3.0-service.rc" ]; then
    setfattr -n security.selinux -v u:object_r:vendor_configs_file:s0 "$OUT/etc/init/android.hardware.keymaster@3.0-service.rc"
fi

# ---- audio HAL 2.0 (audioserver aborts without it; system_server waits on audio_policy) ----
for f in \
    ./bin/hw/android.hardware.audio@2.0-service \
    ./etc/init/android.hardware.audio@2.0-service.rc \
    ./lib/hw/android.hardware.audio@2.0-impl.so \
    ./lib/hw/android.hardware.audio.effect@2.0-impl.so \
    ./lib/hw/audio.primary.universal7570.so \
    ./lib/hw/audio.primary.default.so \
    ./lib/hw/audio.r_submix.default.so \
    ./lib/hw/audio.usb.default.so \
    ./lib/android.hardware.audio@2.0.so \
    ./lib/android.hardware.audio@4.0.so \
    ./lib/android.hardware.audio.common@2.0.so \
    ./lib/android.hardware.audio.common@4.0.so \
    ./lib/android.hardware.audio.common@2.0-util.so \
    ./lib/android.hardware.audio.common-util.so \
    ./lib/android.hardware.audio.effect@2.0.so \
    ./lib/android.hardware.audio.effect@4.0.so \
    ./lib/android.hardware.bluetooth.a2dp@1.0.so \
    ./lib/android.hardware.soundtrigger@2.0.so \
    ./lib/android.hardware.soundtrigger@2.1.so \
    ./lib/libfmq.so ; do
    if [ -e "/mnt/j4/$f" ]; then
        tar --xattrs --xattrs-include='*' -C /mnt/j4 -cf - "$f" | tar --xattrs --xattrs-include='*' -C $OUT -xpf -
        echo "audio: $f"
    else
        echo "audio: MISSING $f"
    fi
done
if [ -e "$OUT/etc/init/android.hardware.audio@2.0-service.rc" ]; then
    setfattr -n security.selinux -v u:object_r:vendor_configs_file:s0 "$OUT/etc/init/android.hardware.audio@2.0-service.rc"
fi

# ---- Samsung audio HAL support libs (needed by audio.primary.universal7570) ----
for f in \
    ./lib/libtinycompress.so \
    ./lib/libtinyalsa.so \
    ./lib/libaudioroute.so \
    ./lib/libfloatingfeature.so \
    ./lib/libsecaudioinfo.so \
    ./lib/libsamsungDiamondVoice.so \
    ./lib/librecordalive.so \
    ./lib/libpreprocessing_nxp.so \
    ./lib/libSamsungPostProcessConvertor.so \
    ./lib/libaudio-ril.so \
    ./lib/libvndsecril-client.so ./lib/libeffects.so ; do
    if [ -e "/mnt/j4/$f" ]; then
        tar --xattrs --xattrs-include='*' -C /mnt/j4 -cf - "$f" | tar --xattrs --xattrs-include='*' -C $OUT -xpf -
        echo "audiolib: $f"
    else
        echo "audiolib: MISSING $f"
    fi
done

if [ -e /mnt/d/r800/donor/libeffectsconfig.so ]; then
    cp /mnt/d/r800/donor/libeffectsconfig.so $OUT/lib/
    setfattr -n security.selinux -v u:object_r:vendor_file:s0 $OUT/lib/libeffectsconfig.so
    echo "audiolib: libeffectsconfig.so"
else
    echo "audiolib: libeffectsconfig.so MISSING at donor path"
fi

# ---- bluetooth HAL (crash loop without it: btHci == nullptr) ----
for f in \
    ./bin/hw/android.hardware.bluetooth@1.0-service \
    ./etc/init/android.hardware.bluetooth@1.0-service.rc \
    ./lib/hw/android.hardware.bluetooth@1.0-impl.so \
    ./lib/libbt-vendor.so \
    ./lib/vendor.samsung.hardware.bluetooth@1.0.so ; do
    if [ -e "/mnt/j4/$f" ]; then
        tar --xattrs --xattrs-include='*' -C /mnt/j4 -cf - "$f" | tar --xattrs --xattrs-include='*' -C $OUT -xpf -
        echo "bt: $f"
    else
        echo "bt: MISSING $f"
    fi
done
if [ -e "$OUT/etc/init/android.hardware.bluetooth@1.0-service.rc" ]; then
    setfattr -n security.selinux -v u:object_r:vendor_configs_file:s0 "$OUT/etc/init/android.hardware.bluetooth@1.0-service.rc"
fi

umount /mnt/j4
cat >> $OUT/build.prop <<'P2'
# ---- graphics: J4 Mali-T720 r28p0 + Samsung gralloc, framebuffer composer (no hwcomposer module) ----
ro.hardware.egl=mali
ro.hardware.gralloc=exynos5
ro.hardware.memtrack=universal7570
ro.opengles.version=196609
debug.sf.hw=1
P2

# our files
S=/mnt/d/r800/vendor_r800_src
cp $S/etc/init/init.r800.rc $OUT/etc/init/; cp $S/etc/fstab.r800 $OUT/etc/; cp $S/ueventd.rc $OUT/
cp $S/etc/vintf/manifest.xml $OUT/etc/vintf/manifest.xml
# Enable PIN/pattern lock screen (LockPatternUtils checks android.software.secure_lock_screen)
cat > $OUT/etc/permissions/android.software.secure_lock_screen.xml <<'SLS'
<?xml version="1.0" encoding="utf-8"?>
<permissions>
    <feature name="android.software.secure_lock_screen" />
</permissions>
SLS
chmod 644 $OUT/etc/permissions/android.software.secure_lock_screen.xml
setfattr -n security.selinux -v u:object_r:vendor_configs_file:s0 $OUT/etc/permissions/android.software.secure_lock_screen.xml
# Drop leftover donor NFC feature declarations (SM-R800 has no NFC HAL; avoids GMS NfcAdapter NPE loop)
rm -f $OUT/etc/permissions/com.nxp.mifare.xml $OUT/etc/permissions/android.hardware.nfc.hce.xml $OUT/etc/permissions/android.hardware.nfc.hcef.xml
# our HWC 1.1 module (client composition -> fb0 copy + pan), built with the NDK
cp $S/hwcomposer.r800.so $OUT/lib/hw/hwcomposer.r800.so
setfattr -n security.selinux -v u:object_r:vendor_file:s0 $OUT/lib/hw/hwcomposer.r800.so
printf 'ro.hardware.hwcomposer=r800
' >> $OUT/build.prop
# lights: Samsung light service + HIDL impl from J4, our legacy module for exynos_panel
mkdir -p /mnt/j4; mountpoint -q /mnt/j4 || mount -o loop,ro /root/android/j4_vendor.img /mnt/j4
( cd /mnt/j4 && tar --xattrs --xattrs-include='*' -cf - ./bin/hw/vendor.samsung.hardware.light@2.0-service ./etc/init/vendor.samsung.hardware.light@2.0-service.rc ./lib/hw/vendor.samsung.hardware.light@2.0-impl.so ./lib/vendor.samsung.hardware.light@2.0.so ) | tar --xattrs --xattrs-include='*' -C $OUT -xpf -
cp $S/lights.r800.so $OUT/lib/hw/lights.r800.so
setfattr -n security.selinux -v u:object_r:vendor_file:s0 $OUT/lib/hw/lights.r800.so
printf 'ro.hardware.lights=r800
' >> $OUT/build.prop
setfattr -n security.selinux -v u:object_r:vendor_configs_file:s0 $OUT/etc/vintf/manifest.xml
setfattr -n security.selinux -v u:object_r:vendor_configs_file:s0 $OUT/etc/init/init.r800.rc $OUT/etc/fstab.r800 $OUT/ueventd.rc
setfattr -n security.selinux -v u:object_r:vendor_file:s0 $OUT
# where bcmdhd looks if Wi-Fi is used later (driver prefixes /vendor): /vendor/data/r800dev/fw
mkdir -p $OUT/data/r800dev/fw
# props: identify as our device, keep the Wear OS characteristics
cat >> $OUT/build.prop <<'P'

# ---- SM-R800 bring-up overrides ----
ro.board.platform=exynos9110
persist.sys.dalvik.vm.lib.2=libart.so
# no GPU driver yet: do not preload EGL in zygote (it aborts when none is found)
ro.zygote.disable_gl_preload=true
ro.hardware.audio.primary=universal7570
P
# identity: this is a Galaxy Watch, not the TicWatch the images came from
sed -i -e 's/^ro.product.vendor.model=.*/ro.product.vendor.model=Galaxy Watch/'        -e 's/^ro.product.vendor.brand=.*/ro.product.vendor.brand=samsung/'        -e 's/^ro.product.vendor.manufacturer=.*/ro.product.vendor.manufacturer=Samsung/' $OUT/build.prop
du -sh $OUT; find $OUT -type f | wc -l
# image: 16 MB ext4, keep xattrs
rm -f ~/android/vendor.img; truncate -s 64M ~/android/vendor.img
mkfs.ext4 -q -b 4096 -m 0 -I 256 -O ^metadata_csum,^64bit -L vendor ~/android/vendor.img
mkdir -p /mnt/dst; mount -o loop ~/android/vendor.img /mnt/dst
tar --xattrs --xattrs-include='*' -C $OUT -cf - . | tar --xattrs --xattrs-include='*' -C /mnt/dst -xpf -
setfattr -n security.selinux -v u:object_r:vendor_file:s0 /mnt/dst
# label anything that came out unlabeled (dirs created by tar, our new dirs)
find /mnt/dst -path /mnt/dst/lost+found -prune -o -print | while read f; do getfattr -n security.selinux "$f" >/dev/null 2>&1 || setfattr -n security.selinux -v u:object_r:vendor_file:s0 "$f"; done
echo "unlabeled left: $(find /mnt/dst -path /mnt/dst/lost+found -prune -o -print | while read f; do getfattr -n security.selinux "$f" >/dev/null 2>&1 || echo x; done | wc -l)"
echo "label check: $(getfattr -n security.selinux --only-values /mnt/dst/bin/vndservicemanager) / $(getfattr -n security.selinux --only-values /mnt/dst/etc/init/init.r800.rc) / $(getfattr -n security.selinux --only-values /mnt/dst/etc/selinux/precompiled_sepolicy)"
ls /mnt/dst /mnt/dst/etc /mnt/dst/etc/init
umount /mnt/dst
e2fsck -fn ~/android/vendor.img | tail -1
cp ~/android/vendor.img /mnt/d/r800/donor/vendor_r800.img; ls -l /mnt/d/r800/donor/vendor_r800.img
