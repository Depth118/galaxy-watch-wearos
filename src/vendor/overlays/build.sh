#!/bin/sh
# Build the static RROs in this directory: sh build.sh <name> (orbita, pinner).
# Install: copy out/<Name>.apk to /oem/r800dev/android/overlay/<Name>/ on the
# watch (dir 755, apk 644); start.sh mirrors it into /product/overlay at boot.
set -e
cd "$(dirname "$0")/$1"
SDK=${ANDROID_SDK:-$HOME/AppData/Local/Android/Sdk}
B=$SDK/build-tools/35.0.0
NAME=R800$(echo "$1" | sed 's/./\U&/')Overlay
rm -rf out && mkdir out
$B/aapt2 compile --dir res -o out/res.zip
$B/aapt2 link -o out/unsigned.apk -I $SDK/platforms/android-34/android.jar \
	--manifest AndroidManifest.xml out/res.zip --no-auto-version
$B/zipalign -f 4 out/unsigned.apk out/aligned.apk
AS=$B/apksigner; [ -x "$AS" ] || AS=$B/apksigner.bat
$AS sign --ks "$HOME/.android/debug.keystore" --ks-pass pass:android \
	--out out/$NAME.apk out/aligned.apk
echo out/$NAME.apk
