#!/bin/sh
# Build the WebAssembly port: the menu and the race as one program
# (port/web/out/a2racer2.{html,js,wasm}).  The C sources are the desktop
# port's own, taken from CMakeLists.txt; the web-only parts are behind
# A2_WEB / __EMSCRIPTEN__ and in this directory.
#
#   port/web/build.sh              (needs Emscripten: source emsdk_env.sh first,
#                                   or have it in .work/emsdk)
#   port/web/serve.sh              to play it at http://localhost:8080/
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
PORT=$(cd "$HERE/.." && pwd)
ROOT=$(cd "$PORT/.." && pwd)

if ! command -v emcc >/dev/null 2>&1; then
    if [ -x "$ROOT/.work/emsdk/upstream/emscripten/emcc" ]; then
        PATH="$ROOT/.work/emsdk/upstream/emscripten:$PATH"
    else
        echo "emcc not found: install Emscripten (emsdk) or source its emsdk_env.sh" >&2
        exit 1
    fi
fi

# the source lists of a2core, a2view and a2menu in CMakeLists.txt
sources() {
    sed -n "/add_$1($2/,/)/p" "$PORT/CMakeLists.txt" | tr -d ' ' | grep '\.c$'
}
SRC=$( (sources library a2core; sources executable a2view; sources executable a2menu) | sort -u)

OUT="$HERE/out"
mkdir -p "$OUT"
cd "$PORT"
# shellcheck disable=SC2086
emcc -O2 ${A2_WEB_CFLAGS:-} -DA2_WEB -std=gnu99 \
    -Isrc -Isrc/menu -include "$HERE/glshim.h" \
    $SRC "$HERE/glshim.c" \
    -sUSE_SDL=2 \
    -sLEGACY_GL_EMULATION=1 -sGL_FFP_ONLY=0 \
    -sASYNCIFY -sASYNCIFY_STACK_SIZE=262144 \
    -sSTACK_SIZE=8MB -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=256MB -sMAXIMUM_MEMORY=4GB \
    -sFORCE_FILESYSTEM=1 -lidbfs.js \
    -sINVOKE_RUN=0 -sEXIT_RUNTIME=0 \
    -sEXPORTED_RUNTIME_METHODS=callMain,FS,IDBFS \
    -sENVIRONMENT=web \
    --shell-file "$HERE/shell.html" \
    -o "$OUT/a2racer2.html"
echo "built $OUT/a2racer2.html"
