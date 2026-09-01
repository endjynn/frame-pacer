#!/bin/sh
set -eu

if command -v sudo >/dev/null 2>&1; then
    elevate=sudo
else
    elevate=
fi

$elevate dpkg --add-architecture i386
$elevate apt-get update
$elevate apt-get install -y --no-install-recommends \
    build-essential \
    gcc-multilib \
    libc6-dev-i386 \
    libvulkan-dev \
    libvulkan-dev:i386 \
    libegl-dev \
    libgl-dev \
    libx11-dev \
    libvulkan1 \
    libvulkan1:i386 \
    mesa-vulkan-drivers \
    mesa-vulkan-drivers:i386 \
    libegl1 \
    libegl1:i386 \
    libgl1 \
    libgl1:i386 \
    libx11-6 \
    libx11-6:i386 \
    jq \
    xxd \
    file \
    glslang-tools \
    spirv-tools \
    binutils \
    xz-utils \
    python3 \
    python3-yaml \
    git \
    ca-certificates
