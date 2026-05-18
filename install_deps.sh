#!/usr/bin/env bash
# Install all vcap build dependencies on Ubuntu/Debian
set -euo pipefail

echo "==> Installing vcap dependencies"
sudo apt-get update -qq

sudo apt-get install -y \
    build-essential \
    cmake \
    pkg-config \
    git \
    \
    libavformat-dev \
    libavcodec-dev \
    libavutil-dev \
    libswscale-dev \
    libavdevice-dev \
    \
    libcurl4-openssl-dev \
    libssl-dev \
    \
    libboost-all-dev \
    \
    libx11-dev \
    libxext-dev \
    xdotool \
    \
    2>/dev/null || true

# yt-dlp (runtime dependency, not build dependency)
if ! command -v yt-dlp &>/dev/null; then
    echo "==> Installing yt-dlp"
    sudo curl -L https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp \
        -o /usr/local/bin/yt-dlp
    sudo chmod +x /usr/local/bin/yt-dlp
else
    echo "==> yt-dlp already installed: $(yt-dlp --version)"
fi

echo ""
echo "All dependencies installed. Run ./build.sh to build."
