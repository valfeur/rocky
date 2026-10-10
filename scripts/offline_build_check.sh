#!/usr/bin/env bash
# rocky-lite offline build check (Linux / bash).
#
# Configures and builds the ROCKY_LITE configuration from scratch in a
# clean directory, with outbound network access made to fail fast, so a
# dependency lookup that unexpectedly tries to reach the network shows up
# as a build failure instead of silently succeeding. This is NOT a true
# network-namespace sandbox: if `unshare --net` is available and usable
# without extra privileges, it's used for a real block; otherwise this
# falls back to pointing every common proxy environment variable at an
# address nothing is listening on, so an accidental HTTP(S) call fails
# immediately with a connection error.
#
# Usage: scripts/offline_build_check.sh [build_dir] [build_type]

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-$REPO_ROOT/build_offline_check}"
BUILD_TYPE="${2:-Release}"

echo "== rocky-lite offline build check =="
echo "Repo:  $REPO_ROOT"
echo "Build: $BUILD_DIR"

rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

BLACKHOLE="http://127.0.0.1:9"   # discard port; nothing should ever answer here
export HTTP_PROXY="$BLACKHOLE" HTTPS_PROXY="$BLACKHOLE"
export http_proxy="$BLACKHOLE" https_proxy="$BLACKHOLE"
unset NO_PROXY no_proxy || true

run_configure_and_build() {
    echo
    echo "-- Configuring (ROCKY_LITE=ON, no toolchain file) --"
    cmake -S "$REPO_ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE" -DROCKY_LITE=ON

    echo
    echo "-- Building --"
    cmake --build "$BUILD_DIR" --config "$BUILD_TYPE" --parallel
}

if command -v unshare >/dev/null 2>&1 && unshare --net true 2>/dev/null; then
    echo "-- Using 'unshare --net' for a real network block --"
    export -f run_configure_and_build
    unshare --net bash -c run_configure_and_build
else
    echo "-- 'unshare --net' unavailable; falling back to black-hole proxy env vars --"
    run_configure_and_build
fi

echo
echo "-- Checking for vcpkg/system package-manager leakage --"
if grep -q "vcpkg" "$BUILD_DIR/CMakeCache.txt" 2>/dev/null; then
    if grep "CMAKE_TOOLCHAIN_FILE" "$BUILD_DIR/CMakeCache.txt" | grep -q "vcpkg"; then
        echo "CMAKE_TOOLCHAIN_FILE references vcpkg -- lite build must not need it" >&2
        exit 1
    fi
fi

echo
echo "OFFLINE BUILD CHECK PASSED"
