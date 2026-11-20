#!/system/bin/sh
# Put the watch back to "not paired" so the setup wizard can pair with the phone's
# Pixel Watch app again. On the phone first: forget the watch in Bluetooth settings
# and clear the Pixel Watch app's storage. Run as root from the telnet shell.
# Keep the watch on the charger: below 21% the wizard parks on a battery screen.
touch /data/r800-pair-mode
svc bluetooth disable; sleep 3
for p in com.google.android.gms com.google.android.wearable.app \
         com.google.android.wearable.setupwizard com.google.android.apps.wearable.bluetooth \
         com.google.android.clockwork.oemsetup com.google.android.wearable.watchface.rwf; do
    echo "$p $(pm clear $p)"
done
rm -f /data/misc/bluedroid/bt_config.conf /data/misc/bluedroid/bt_config.bak
settings put secure user_setup_complete 0
# 1 makes the wizard treat this as an upgrade: it skips the phone-driven stage
# (status/progression) and exits at once, leaving the phone on "Connecting"
settings put system setup_wizard_has_run 0
settings put global device_provisioned 0
# WCS publishes /setup/oem_setup_info (product name the phone app identifies the
# watch by) only while this is unset; clearing GMS above deleted the old item.
# Left at 1, the phone fails with CAPABILITIES_READ_ERROR on "Connecting".
settings delete global oem_setup_version
settings put global bluetooth_on 1
svc bluetooth enable; sleep 5
# the wizard disables its entry activity when it finishes
pm enable com.google.android.wearable.setupwizard/.core.SetupWizardActivity
pm enable com.google.android.wearable.setupwizard/.steps.UpgradeActivity
# The wizard is the home app: force-stopping it makes Android relaunch it as home
# by itself. Starting it explicitly as well leaves two copies, and a stale copy
# (e.g. a battery screen waiting for >20%) later jumps the flow back to pairing.
am force-stop com.google.android.wearable.setupwizard
sleep 6
dumpsys activity activities | grep -q 'mResumedActivity.*setupwizard' || \
    am start -a android.intent.action.MAIN -c android.intent.category.HOME
