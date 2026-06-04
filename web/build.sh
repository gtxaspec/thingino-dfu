#!/bin/bash
# Build thingino-dfu for WebAssembly
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
EMSDK_DIR="$REPO_DIR/emsdk"

# Fetch the Emscripten SDK if not already present. Not vendored as a submodule -
# cloned fresh (matching CI), and emsdk/ is gitignored. Delete emsdk/ to upgrade.
if [ ! -f "$EMSDK_DIR/emsdk" ]; then
    git clone --depth 1 https://github.com/emscripten-core/emsdk.git "$EMSDK_DIR"
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
