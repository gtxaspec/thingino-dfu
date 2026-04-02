#!/bin/bash
# Build thingino-cloner for WebAssembly
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
EMSDK_DIR="$REPO_DIR/emsdk"

# Bootstrap emsdk submodule if needed
if [ ! -f "$EMSDK_DIR/emsdk" ]; then
    git -C "$REPO_DIR" submodule update --init --recursive emsdk
fi

# Install/activate the latest upstream SDK if emcc is not yet available
if [ ! -f "$EMSDK_DIR/upstream/emscripten/emcc" ]; then
    "$EMSDK_DIR/emsdk" install latest
    "$EMSDK_DIR/emsdk" activate latest
fi

source "$EMSDK_DIR/emsdk_env.sh"

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

emcmake cmake "$SCRIPT_DIR" -DCMAKE_BUILD_TYPE=Release
emmake make -j$(nproc) VERBOSE=1

# Bundle the web app with Vite
cd "$SCRIPT_DIR"
[ ! -d node_modules ] && npm ci
npm run build

echo ""
echo "Build complete. Output in $SCRIPT_DIR/dist/"
ls -la "$SCRIPT_DIR/dist/"
