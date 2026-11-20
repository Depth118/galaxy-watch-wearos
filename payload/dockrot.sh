#!/system/bin/sh
# dockrot.sh: turn the Wear OS display to how the watch sits on the charger (the
# stock dock holds it on its side). Started by android-helper.sh; $1 = its $TZE
# (env prefix for the Tizen chroot). Gravity on +X = ROTATION_90 (verified on the
# dock 2026-11-20), -X = 270, -Y = 180, +Y or flat = 0; back to 0 off the charger.
#
# Charger state is checked every second (shell read, no fork); on power the
# accelerometer is sampled once a second through the hub's sensorctl, stopping at
# the first sample (~0.25 s). A turn needs two equal readings in a row, so
# picking the watch up does not flip the screen around.
B=/oem/r800dev/bin/busybox
TZE=${1:-/usr/bin/env}
AC=/sys/class/power_supply/ac/online
k() { echo "<6>r800: $*" > /dev/kmsg; }
setrot() {
    /system/bin/settings put system accelerometer_rotation 0 &&
        /system/bin/settings put system user_rotation $1 && {
        k "dock rotation $ROT -> $1"
        ROT=$1
    }
}
sample() {
    $B timeout 3 $B chroot /mnt/tz $TZE /usr/bin/sensorctl test accelerometer 2>/dev/null < /dev/null |
        $B awk '$1 ~ /^[0-9]+$/ && NF >= 4 {
                    x = $2; y = $3
                    if (x > 6) print 1; else if (x < -6) print 3; else if (y < -6) print 2; else print 0
                    exit }'
}
until [ "$(/system/bin/getprop sys.boot_completed)" = 1 ]; do $B sleep 5; done
ROT=$(/system/bin/settings get system user_rotation)
LAST=-1
while true; do
    read -r on < $AC 2>/dev/null || on=0
    if [ "$on" = 1 ]; then
        R=$(sample)
        if [ -n "$R" ] && [ "$R" = "$LAST" ] && [ "$R" != "$ROT" ]; then
            setrot $R
        fi
        LAST=${R:--1}
    else
        LAST=-1
        [ "$ROT" != 0 ] && setrot 0
    fi
    $B sleep 1
done
