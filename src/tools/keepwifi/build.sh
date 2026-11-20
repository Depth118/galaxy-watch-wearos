#!/bin/sh
# Build keepwifi.dex (see KeepWifi.java). Needs a JDK and the Android SDK.
set -e
cd "$(dirname "$0")"
SDK=${ANDROID_SDK:-$HOME/AppData/Local/Android/Sdk}
rm -rf out && mkdir -p out/classes
javac --release 8 -cp "$SDK/platforms/android-34/android.jar" -d out/classes KeepWifi.java
D8="$SDK/build-tools/35.0.0/d8"; [ -x "$D8" ] || D8="$D8.bat"
"$D8" --min-api 28 --output out out/classes/*.class
mv out/classes.dex out/keepwifi.dex
echo out/keepwifi.dex
