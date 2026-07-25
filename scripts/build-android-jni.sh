#!/bin/sh
# Build libtdfu_jni.so for every supported Android ABI and package the result.
#
# The output tarball is the entire Android dependency: the JNI bridge, all of
# libtdfu, and a statically linked libusb, leaving only Android system libraries
# as external references. A consumer drops the .so files into an APK's
# jniLibs/<abi>/ and needs no C toolchain of its own.
#
# Usage: scripts/build-android-jni.sh [output-dir]
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

	mkdir -p "$STAGE/$ABI"
	cp "$BUILD/$ABI/libtdfu_jni.so" "$STAGE/$ABI/libtdfu_jni.so"
	"$STRIP" --strip-unneeded "$STAGE/$ABI/libtdfu_jni.so"

	# A dependency on anything outside the Android system set means libusb or
	# libtdfu failed to link statically, which would surface as a dlopen error
	# on device rather than here. Catch it at build time instead.
	BAD=$("$READELF" -d "$STAGE/$ABI/libtdfu_jni.so" \
		| sed -n 's/.*Shared library: \[\(.*\)\]/\1/p' \
		| grep -vE '^(liblog|libandroid|libm|libdl|libc)\.so$' || true)
	if [ -n "$BAD" ]; then
		echo "unexpected shared dependency in $ABI: $BAD" >&2
		exit 1
	fi

	# Renaming the Kotlin package silently breaks statically registered JNI, so
	# assert the full export set rather than trusting the build succeeded.
	N=$("$READELF" --dyn-syms "$STAGE/$ABI/libtdfu_jni.so" \
		| grep -c 'Java_com_thingino_dfu_TdfuBridge_' || true)
	[ "$N" -eq 10 ] || { echo "expected 10 JNI exports in $ABI, found $N" >&2; exit 1; }

	echo "    $(stat -c%s "$STAGE/$ABI/libtdfu_jni.so" 2>/dev/null || echo '?') bytes, $N JNI exports"
done

cat >"$STAGE/README" <<EOF
libtdfu_jni.so $VERSION

Prebuilt JNI library for thingino-dfu. Contains the JNI bridge, libtdfu, and a
statically linked libusb. Only Android system libraries are external.

Drop each <abi>/libtdfu_jni.so into your APK at src/main/jniLibs/<abi>/ and
declare the bindings from a Kotlin class named com.thingino.dfu.TdfuBridge.
The Java package is baked into the exported symbol names and is unrelated to
your applicationId, so keep that package name whatever your app is called.

Minimum supported API level: 26.
EOF

TARBALL=$OUT/libtdfu-jni-android-$VERSION.tar.gz
tar -czf "$TARBALL" -C "$STAGE" .
echo "==> $TARBALL"
