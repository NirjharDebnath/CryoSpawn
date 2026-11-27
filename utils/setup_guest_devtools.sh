#!/usr/bin/env bash
set -euo pipefail

# CryoSpawn Guest / RootFS Chroot Dev-Tools Installer
# Installs C/C++, Python 3, networking/process utilities, and common CLI tools.

echo "=========================================="
echo "  CryoSpawn DevTools & Utilities Setup    "
echo "=========================================="

# 1. Ensure /tmp, /var/tmp, and apt partial directories exist with proper permissions
chmod 1777 /tmp
[ -d /var/tmp ] && chmod 1777 /var/tmp || true
mkdir -p /var/cache/apt/archives/partial /var/lib/apt/lists/partial /var/log/apt

# 2. Ensure public DNS is configured (router 192.168.x.x DNS is blocked by VM firewall)
if ! grep -q '^nameserver ' /etc/resolv.conf 2>/dev/null; then
    echo "[*] Configuring public DNS (1.1.1.1 / 8.8.8.8)..."
    rm -f /etc/resolv.conf
    printf "nameserver 1.1.1.1\nnameserver 8.8.8.8\n" > /etc/resolv.conf
fi

export DEBIAN_FRONTEND=noninteractive

echo "[*] Updating apt package index..."
apt-get update -qq

echo "[*] Installing C/C++ toolchain, Python 3, and essential CLI tools..."
apt-get install -y -qq -o Dpkg::Options::="--force-confdef" -o Dpkg::Options::="--force-confold" \
    build-essential \
    gcc \
    g++ \
    cmake \
    ninja-build \
    gdb \
    pkg-config \
    python3 \
    python3-pip \
    python3-venv \
    python3-dev \
    git \
    curl \
    wget \
    nano \
    vim \
    htop \
    tmux \
    jq \
    unzip \
    ca-certificates \
    netcat-openbsd \
    iproute2 \
    net-tools \
    dnsutils \
    util-linux

# Install neofetch (or fastfetch if neofetch is unavailable on newer Ubuntu)
if apt-cache show neofetch >/dev/null 2>&1; then
    apt-get install -y -qq -o Dpkg::Options::="--force-confdef" -o Dpkg::Options::="--force-confold" neofetch
elif apt-cache show fastfetch >/dev/null 2>&1; then
    apt-get install -y -qq -o Dpkg::Options::="--force-confdef" -o Dpkg::Options::="--force-confold" fastfetch
fi

# Ensure direct ssh.service is enabled (Ubuntu 24.04 ssh.socket hangs on older Firecracker kernels)
if command -v systemctl >/dev/null 2>&1 && [ -f /usr/lib/systemd/system/ssh.service ]; then
    systemctl disable ssh.socket 2>/dev/null || true
    systemctl enable ssh.service 2>/dev/null || true
fi

echo "[*] Cleaning apt cache to keep RootFS image compact..."
apt-get clean
rm -rf /var/lib/apt/lists/*

echo ""
echo "[✓] Installation complete! Installed versions:"
gcc --version | head -n 1
g++ --version | head -n 1
python3 --version
cmake --version | head -n 1
git --version
command -v nc setsid nano neofetch fastfetch || true
