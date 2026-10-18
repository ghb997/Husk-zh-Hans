#!/bin/bash
# Builds the PS4 layer for the phone: husk-fex's FEXCore and Bachata-S4's core (branch husk) as libhuskps4.dylib, copied to
# build/ios-arm64/lib for project.yml to embed.
#
#   scripts/build_ps4_ios.sh            # build with the trees at BACHATA_DIR and HUSK_FEX_DIR
#
# BACHATA_DIR (default ~/Documents/Coding/AetherCore4/Bachata-S4) must be on branch husk; HUSK_FEX_DIR (default
# ~/Documents/Coding/husk-fex) on branch husk-fex. Their commits are printed, and written next to the dylib, so a build says
# what it was made from.
set -euo pipefail
HUSK="$(cd "$(dirname "$0")/.." && pwd)"
BACHATA="${BACHATA_DIR:-$HOME/Documents/Coding/AetherCore4/Bachata-S4}"
FEX="${HUSK_FEX_DIR:-$HOME/Documents/Coding/husk-fex}"
FEX_SRC="$FEX/runtime/sources/fexcore-darwin"
FEX_BUILD="$BACHATA/runtime/build/fexcore-huskfex-ios"
CORE_BUILD="$BACHATA/runtime/build/huskps4-ios"
OUT="$HUSK/build/ios-arm64/lib"
JOBS="${JOBS:-$(sysctl -n hw.ncpu)}"

[ "$(git -C "$BACHATA" branch --show-current)" = husk ] || { echo "error: $BACHATA is not on branch husk" >&2; exit 1; }
[ "$(git -C "$FEX" branch --show-current)" = husk-fex ] || { echo "error: $FEX is not on branch husk-fex" >&2; exit 1; }
CORE_REV="$(git -C "$BACHATA" rev-parse --short HEAD)"; FEX_REV="$(git -C "$FEX" rev-parse --short HEAD)"
echo "PS4 layer: core $CORE_REV, FEXCore $FEX_REV"

if [ ! -f "$FEX_BUILD/CMakeCache.txt" ]; then
    cmake -S "$FEX_SRC" -B "$FEX_BUILD" -G "Unix Makefiles" -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphoneos \
        -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=17.4 -DCMAKE_SYSTEM_PROCESSOR=arm64 -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_FEXCORE_ONLY=ON -DBUILD_FEXCORE_SMOKE=OFF -DBUILD_FEXCONFIG=OFF -DBUILD_TESTING=OFF -DBUILD_THUNKS=OFF \
        -DENABLE_LTO=OFF -DENABLE_CCACHE=ON -DENABLE_ASSERTIONS=OFF -DENABLE_JEMALLOC_GLIBC_ALLOC=OFF -DENABLE_OFFLINE_TELEMETRY=OFF \
        -DENABLE_FEX_ALLOCATOR=OFF -DTUNE_ARCH=generic -DTUNE_CPU=none -DCMAKE_CXX_FLAGS=-DMETA_NO_STD_FORWARD_DECLARATIONS
fi
# FEX's rpmalloc maps 256 MiB spans per thread and page type: tens of GB of address space for a game with ~30 threads,
# more than iOS's large-mapping window has room for beside the PS4's own reservations. The system allocator instead.
if ! grep -q "^ENABLE_FEX_ALLOCATOR:BOOL=OFF" "$FEX_BUILD/CMakeCache.txt"; then
    cmake -DENABLE_FEX_ALLOCATOR=OFF "$FEX_BUILD"
    rm -f "$FEX_BUILD/External/rpmalloc/librpmalloc.a"
    RECONFIGURE_CORE=1   # the core's CMake lists FEXCore's libraries as they exist when it configures
fi
make -C "$FEX_BUILD" -j"$JOBS" FEXCore Common CommonTools JemallocLibs

if [ ! -f "$CORE_BUILD/CMakeCache.txt" ]; then
    cmake -S "$BACHATA" -B "$CORE_BUILD" -G Ninja -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphoneos -DCMAKE_OSX_ARCHITECTURES=arm64 \
        -DCMAKE_OSX_DEPLOYMENT_TARGET=17.4 -DCMAKE_BUILD_TYPE=Release -DENABLE_FEX_GUEST_CPU=ON \
        -DFEXCORE_GUEST_CPU_BUILD_DIR="$FEX_BUILD" -DFEXCORE_GUEST_CPU_SOURCE_DIR="$FEX_SRC" \
        -DENABLE_SHADPS4_IOS_LIB=ON -DENABLE_HUSK_MODULE=ON -DENABLE_DISCORD_RPC=OFF -DENABLE_UPDATER=OFF \
        -DIMGUI_FONT_EMBED_EXECUTABLE="$BACHATA/runtime/build/host-tools/Dear_ImGui_FontEmbed" \
        -DALLOWS_ONESHOT_TIMERS_WITH_TIMEOUT_ZERO_EXITCODE=0
elif [ "${RECONFIGURE_CORE:-0}" = 1 ]; then
    cmake "$CORE_BUILD"
fi
ninja -C "$CORE_BUILD" huskps4

DYLIB="$CORE_BUILD/libhuskps4.dylib"
exports="$(nm -gU "$DYLIB" | awk '{print $3}')"
[ "$exports" = "_husk_console_module_v1" ] || { echo "error: libhuskps4 exports more than husk_console_module_v1:" >&2; echo "$exports" >&2; exit 1; }
if nm -u "$DYLIB" | grep -q BreakGetJITMapping; then echo "error: libhuskps4 still references BreakpointJIT" >&2; exit 1; fi
mkdir -p "$OUT"
install_name_tool -id @rpath/libhuskps4.dylib "$DYLIB"
cp "$DYLIB" "$OUT/libhuskps4.dylib"
echo "core $CORE_REV (Bachata-S4 husk), FEXCore $FEX_REV (husk-fex)" > "$OUT/libhuskps4.revision"
echo "libhuskps4.dylib: $(du -h "$OUT/libhuskps4.dylib" | cut -f1) -> $OUT"
