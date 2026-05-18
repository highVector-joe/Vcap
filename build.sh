#!/usr/bin/env bash
# vcap build script
# Usage: ./build.sh [--debug] [--clean]
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
BUILD_TYPE="Release"

for arg in "$@"; do
    case $arg in
        --debug)  BUILD_TYPE="Debug"   ;;
        --clean)  rm -rf "$BUILD_DIR"  ;;
    esac
done

echo "==> vcap build  [type=$BUILD_TYPE]"

# ── Dependency check ────────────────────────────────────────────────────────
check_dep() {
    local pkg="$1"
    local pkg_name="${2:-$1}"
    if ! pkg-config --exists "$pkg" 2>/dev/null; then
        echo "  MISSING: $pkg_name"
        return 1
    fi
    echo "  OK: $pkg_name $(pkg-config --modversion "$pkg" 2>/dev/null)"
}

echo ""
echo "── Checking dependencies ──────────────────────────────────────────────"
MISSING=0

check_dep libavformat  "FFmpeg (libavformat)"  || MISSING=1
check_dep libavcodec   "FFmpeg (libavcodec)"   || MISSING=1
check_dep libavutil    "FFmpeg (libavutil)"    || MISSING=1

if ! command -v curl-config &>/dev/null && ! pkg-config --exists libcurl 2>/dev/null; then
    echo "  MISSING: libcurl"
    MISSING=1
else
    echo "  OK: libcurl"
fi

# Boost
if ! dpkg -s libboost-dev &>/dev/null 2>&1 && \
   ! brew list boost &>/dev/null 2>&1 && \
   ! pkg-config --exists boost &>/dev/null 2>&1; then
    echo "  MISSING: Boost >= 1.74"
    MISSING=1
else
    echo "  OK: Boost"
fi

if ! pkg-config --exists openssl 2>/dev/null; then
    echo "  MISSING: OpenSSL"
    MISSING=1
else
    echo "  OK: OpenSSL $(pkg-config --modversion openssl)"
fi

# Optional
echo ""
echo "── Optional dependencies ───────────────────────────────────────────────"
pkg-config --exists x11 libxext 2>/dev/null && echo "  OK: X11 + XShm (screen capture)" || echo "  SKIP: X11 (screen capture disabled)"
pkg-config --exists libpipewire-0.3 2>/dev/null && echo "  OK: PipeWire (Wayland screen capture)" || echo "  SKIP: PipeWire"
command -v yt-dlp &>/dev/null && echo "  OK: yt-dlp $(yt-dlp --version 2>/dev/null)" || echo "  SKIP: yt-dlp not in PATH (Layer 2 will warn at runtime)"

if [ "$MISSING" -ne 0 ]; then
    echo ""
    echo "ERROR: Install missing dependencies and re-run."
    echo ""
    echo "  Ubuntu/Debian:"
    echo "    sudo apt install build-essential cmake pkg-config \\"
    echo "      libavformat-dev libavcodec-dev libavutil-dev libswscale-dev \\"
    echo "      libcurl4-openssl-dev libboost-all-dev libssl-dev \\"
    echo "      libx11-dev libxext-dev"
    echo ""
    echo "  Fedora/RHEL:"
    echo "    sudo dnf install cmake pkgconf ffmpeg-devel libcurl-devel \\"
    echo "      boost-devel openssl-devel libX11-devel libXext-devel"
    exit 1
fi

echo ""
echo "── Building ────────────────────────────────────────────────────────────"

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

cmake .. \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_INSTALL_PREFIX=/usr/local \
    2>&1 | tail -20

cmake --build . --parallel "$(nproc)"

echo ""
echo "── Done ────────────────────────────────────────────────────────────────"
echo "  Binary: $BUILD_DIR/vcap"
echo ""
echo "  Run:  $BUILD_DIR/vcap --help"
echo "  Install (optional): sudo cmake --install $BUILD_DIR"
echo ""
