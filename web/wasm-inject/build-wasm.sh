#!/bin/bash
# Build the client-side overlay-injection engines to WebAssembly (MEMFS ES
# modules) from a pinned mtd-utils, with small emscripten/musl compat stubs:
#
#   mkfs_jffs2_memfs   - repack the NOR JFFS2 overlay
#   mkfs_ubifs_memfs   - fresh UBIFS overlay volume (NAND)
#   ubinize_memfs      - reassemble the NAND UBI image
#
# Output -> web/public/wasm/ (alongside tdfu.wasm). The Emscripten env must be
# active (web/build.sh sources it before calling this).
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
OUT="$DIR/../public/wasm"
SRC="$DIR/mtd-utils-2.3.1"

command -v emcc >/dev/null || { echo "build-wasm: emcc not on PATH (source emsdk first)" >&2; exit 1; }

# 1. fresh extract of the pinned source
rm -rf "$SRC"
tar -xf "$DIR/mtd-utils-2.3.1.tar.bz2" -C "$DIR"

# 2. drop in the compat headers (these files don't exist in stock mtd-utils)
cp "$DIR/stubs/linux/"*.h "$SRC/include/linux/"
mkdir -p "$SRC/include/asm"; cp "$DIR/stubs/asm/byteorder.h" "$SRC/include/asm/"
mkdir -p "$SRC/stub/uuid"
cp "$DIR/stubs/uuid.h" "$SRC/stub/uuid.h"
cp "$DIR/stubs/uuid/uuid.h" "$SRC/stub/uuid/uuid.h"
cp "$DIR/stubs/uuid_stub.c" "$SRC/stub/"

# 3. two in-source edits: ubi-media.h needs the __beNN types; the ubifs
#    linux_types.h compat header needs the kernel inline/attribute macros.
sed -i '/#define __UBI_MEDIA_H__/a #include <linux/types.h>' "$SRC/include/mtd/ubi-media.h"
LT="$SRC/ubifs-utils/common/linux_types.h"
head -n -1 "$LT" > "$LT.tmp"
cat "$DIR/stubs/linux_types_compat.h" >> "$LT.tmp"
echo "#endif" >> "$LT.tmp"
mv "$LT.tmp" "$LT"

cd "$SRC"

# 4. tiny libuuid stub (uuid_generate_random + uuid_unparse_upper)
emcc -O2 -Istub -c stub/uuid_stub.c -o stub/uuid_stub.o
emar rcs stub/libuuid_stub.a stub/uuid_stub.o

# 5. configure (ubifs enabled via the uuid stub) and compile the objects + libs
emconfigure ./configure --without-tests --host=wasm32-unknown-emscripten \
  UUID_CFLAGS="-I$PWD/stub" UUID_LIBS="$PWD/stub/libuuid_stub.a" >/dev/null
emmake make mkfs.jffs2 mkfs.ubifs ubinize CFLAGS="-O2 -Wno-error" >/dev/null

# 6. relink as MEMFS ES modules for the browser (run in-memory, files via JS)
MEMFS="-sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORTED_RUNTIME_METHODS=callMain,FS \
  -sINVOKE_RUN=0 -sEXIT_RUNTIME=0 -sFORCE_FILESYSTEM=1 -sALLOW_MEMORY_GROWTH=1"
mkdir -p "$OUT"
emcc jffsX-utils/mkfs_jffs2-*.o libmtd.a -o "$OUT/mkfs_jffs2_memfs.mjs" $MEMFS
emcc $(find . -name 'mkfs_ubifs-*.o') libmtd.a libubi.a libmissing.a stub/libuuid_stub.a \
  -o "$OUT/mkfs_ubifs_memfs.mjs" $MEMFS
emcc ubi-utils/ubinize.o libubi.a libubigen.a libmtd.a libiniparser.a \
  -o "$OUT/ubinize_memfs.mjs" $MEMFS

echo "build-wasm: built mkfs_jffs2 / mkfs_ubifs / ubinize -> $OUT"
