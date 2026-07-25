#!/bin/sh
# Package everything an Android app needs from this repo.
#
# Two things, and both are easy to forget one of:
#   jniLibs/<abi>/libtdfu_jni.so  the JNI bridge, all of libtdfu, and a
#                                 statically linked libusb, leaving only
#                                 Android system libraries external
#   firmware/                     the tpl.bin/uboot.bin bootstrap blobs, which
#                                 the app ships as assets and cannot flash without
#
# A consumer unpacks these straight into its sourceSets and needs no C toolchain.
#
# Usage: scripts/build-android-deps.sh [output-dir]
#   ANDROID_NDK or ANDROID_NDK_HOME must point at an NDK r25 or newer.

set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT=${1:-$ROOT/dist}
BUILD=$ROOT/build-android-jni
ABIS='arm64-v8a armeabi-v7a'
PLATFORM=android-26

NDK=${ANDROID_NDK:-${ANDROID_NDK_HOME:-}}
if [ -z "$NDK" ] || [ ! -d "$NDK" ]; then
	echo "Set ANDROID_NDK (or ANDROID_NDK_HOME) to an NDK r25+ install." >&2
	exit 1
fi

TOOLCHAIN=$NDK/build/cmake/android.toolchain.cmake
[ -f "$TOOLCHAIN" ] || { echo "No toolchain file at $TOOLCHAIN" >&2; exit 1; }

# The host triple under prebuilt/ varies (linux-x86_64, darwin-x86_64, ...) so
# glob for it. Note llvm-strip is a symlink to llvm-objcopy, so test with -x
# rather than looking for a regular file.
NDK_BIN=
for d in "$NDK"/toolchains/llvm/prebuilt/*/bin; do
	[ -d "$d" ] && NDK_BIN=$d && break
done
STRIP=$NDK_BIN/llvm-strip
READELF=$NDK_BIN/llvm-readelf
for t in "$STRIP" "$READELF"; do
	[ -x "$t" ] || { echo "Missing NDK tool: $t" >&2; exit 1; }
done

VERSION=$(git -C "$ROOT" describe --tags --abbrev=0 2>/dev/null | sed 's/^v//')
[ -n "$VERSION" ] || VERSION=0.0.0

STAGE=$BUILD/stage
rm -rf "$STAGE"
mkdir -p "$STAGE" "$OUT"

for ABI in $ABIS; do
	echo "==> $ABI"
	cmake -B "$BUILD/$ABI" -S "$ROOT/android-jni" \
		-DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
		-DANDROID_ABI="$ABI" \
		-DANDROID_PLATFORM="$PLATFORM" \
		-DCMAKE_BUILD_TYPE=Release >/dev/null
	cmake --build "$BUILD/$ABI" -j"$(nproc 2>/dev/null || echo 4)" >/dev/null

	mkdir -p "$STAGE/jniLibs/$ABI"
	cp "$BUILD/$ABI/libtdfu_jni.so" "$STAGE/jniLibs/$ABI/libtdfu_jni.so"
	"$STRIP" --strip-unneeded "$STAGE/jniLibs/$ABI/libtdfu_jni.so"

	# A dependency on anything outside the Android system set means libusb or
	# libtdfu failed to link statically, which would surface as a dlopen error
	# on device rather than here. Catch it at build time instead.
	BAD=$("$READELF" -d "$STAGE/jniLibs/$ABI/libtdfu_jni.so" \
		| sed -n 's/.*Shared library: \[\(.*\)\]/\1/p' \
		| grep -vE '^(liblog|libandroid|libm|libdl|libc)\.so$' || true)
	if [ -n "$BAD" ]; then
		echo "unexpected shared dependency in $ABI: $BAD" >&2
		exit 1
	fi

	# Renaming the Kotlin package silently breaks statically registered JNI, so
	# assert the full export set rather than trusting the build succeeded.
	N=$("$READELF" --dyn-syms "$STAGE/jniLibs/$ABI/libtdfu_jni.so" \
		| grep -c 'Java_com_thingino_dfu_TdfuBridge_' || true)
	[ "$N" -eq 10 ] || { echo "expected 10 JNI exports in $ABI, found $N" >&2; exit 1; }

	echo "    $(stat -c%s "$STAGE/jniLibs/$ABI/libtdfu_jni.so" 2>/dev/null || echo '?') bytes, $N JNI exports"
done

# The bootstrap blobs. Without these the app can detect and talk to a SoC but
# has nothing to upload to it, so they are part of the dependency, not an extra.
echo "==> firmware"
cp -r "$ROOT/firmware" "$STAGE/firmware"
find "$STAGE/firmware" -name '.gitkeep' -delete
BLOBS=$(find "$STAGE/firmware" -name '*.bin' | wc -l)
[ "$BLOBS" -gt 0 ] || { echo "no firmware blobs staged" >&2; exit 1; }
echo "    $BLOBS blobs, $(du -sh "$STAGE/firmware" | cut -f1)"

cat >"$STAGE/README" <<EOF
thingino-dfu Android dependencies $VERSION

jniLibs/<abi>/libtdfu_jni.so
    The JNI bridge, libtdfu, and a statically linked libusb. Only Android
    system libraries are external, so no NDK or C toolchain is needed to
    consume it. Copy into your APK at src/main/jniLibs/<abi>/.

    Declare the bindings from a Kotlin class named com.thingino.dfu.TdfuBridge.
    That Java package is baked into the exported symbol names and is unrelated
    to your applicationId, so keep it whatever your app is called. Renaming it
    compiles and installs cleanly, then throws UnsatisfiedLinkError on the
    first flash.

firmware/
    Bootstrap blobs, tpl.bin and uboot.bin per SoC variant. Copy into your APK
    at src/main/assets/firmware/. Flashing cannot work without them.

Minimum supported API level: 26.
EOF

TARBALL=$OUT/thingino-dfu-android-deps-$VERSION.tar.gz
tar -czf "$TARBALL" -C "$STAGE" .
echo "==> $TARBALL ($(du -h "$TARBALL" | cut -f1))"
