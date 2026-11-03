#!/usr/bin/env bash
# Fetches, builds and installs the ReXGlue SDK revision this project is
# developed against, under thirdparty/rexglue-sdk. The install registers
# itself with CMake, so the project's presets find it with no extra flags.
#
# Requirements (Debian/Ubuntu names): clang-20 lld-20 cmake ninja-build git
#   libgtk-3-dev libx11-xcb-dev libxss-dev libvulkan-dev libwayland-dev
#   wayland-protocols libxkbcommon-dev libdecor-0-dev libasound2-dev
#   libpulse-dev libpipewire-0.3-dev autoconf python3
set -euo pipefail

REXGLUE_URL=${REXGLUE_URL:-https://github.com/rexglue/rexglue-sdk}
# rexglue-sdk main as of 2026-08-21 (0.10.0-dev). Bump deliberately: the SDK
# API is still moving and codegen output changes between revisions.
REXGLUE_REV=${REXGLUE_REV:-c94f5ebdcb3c9d1a460ca48e04f9758448f8d518}
CC=${CC:-clang-20}
CXX=${CXX:-clang++-20}

root=$(cd "$(dirname "$0")/.." && pwd)
sdk="$root/thirdparty/rexglue-sdk"

if [ ! -d "$sdk/.git" ]; then
    git init -q "$sdk"
    git -C "$sdk" remote add origin "$REXGLUE_URL"
fi
# A run stopped half-way leaves git's lock files behind; nothing else uses
# this copy of the SDK, so they are stale.
if [ -d "$sdk/.git" ]; then
    find "$sdk/.git" -type f \( -name index.lock -o -name shallow.lock -o -name HEAD.lock \
        -o -name config.lock -o -name packed-refs.lock \) -print -delete |
        sed 's/^/Removed a stale git lock: /'
fi
# A run stopped half-way can leave a damaged .gitmodules, which git reads even
# to fetch; the checkout below restores it.
if [ -f "$sdk/.gitmodules" ] && ! git config -f "$sdk/.gitmodules" --list >/dev/null 2>&1; then
    echo "Removing a damaged .gitmodules left by an earlier run"
    rm -f "$sdk/.gitmodules"
fi
git -C "$sdk" fetch --depth 1 origin "$REXGLUE_REV"
git -C "$sdk" checkout -q --force FETCH_HEAD
# Remove files added by previously applied patches (out/ is git-ignored and kept).
git -C "$sdk" clean -fdq
git -C "$sdk" submodule update --init --recursive --depth 1 --jobs 8
# Local fixes to the pinned SDK, applied in name order.
for patch in "$root"/patches/rexglue-sdk/*.patch; do
    [ -e "$patch" ] || continue
    echo "Applying $(basename "$patch")"
    git -C "$sdk" apply --whitespace=nowarn "$patch"
done

(
    cd "$sdk"
    cmake --preset linux-amd64 -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX"
    cmake --build out/build/linux-amd64 --config Release --target install --parallel
)

echo
echo "ReXGlue SDK installed to $sdk/out/install/linux-amd64"
echo "Next: cmake --preset linux-amd64-release && cmake --build out/build/linux-amd64-release"
