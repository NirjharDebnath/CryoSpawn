#!/usr/bin/env bash
set -euo pipefail

# CryoSpawn Asset Downloader
# Downloads compatible Linux guest kernel and base rootfs for Firecracker MicroVMs.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

ARCH="$(uname -m)"
if [ "$ARCH" != "x86_64" ] && [ "$ARCH" != "aarch64" ]; then
    echo "[-] Unsupported architecture: $ARCH"
    exit 1
fi

mkdir -p "${ROOT_DIR}/vmlinux"
mkdir -p "${ROOT_DIR}/rootfs"

KERNEL_DEST="${ROOT_DIR}/vmlinux/ubuntu-vmlinux.bin"
ROOTFS_DEST="${ROOT_DIR}/rootfs/ubuntu-rootfs.ext4"

echo "=========================================="
echo "    CryoSpawn Asset Downloader            "
echo "=========================================="
echo "[+] Target Architecture: ${ARCH}"
echo "[+] Kernel Path       : ${KERNEL_DEST}"
echo "[+] Rootfs Path       : ${ROOTFS_DEST}"
echo ""

# 1. Download Kernel if missing
if [ -f "$KERNEL_DEST" ]; then
    echo "[✓] Kernel already exists at ${KERNEL_DEST}"
else
    echo "[*] Downloading compatible Firecracker Linux kernel..."
    KERNEL_URL="https://s3.amazonaws.com/spec.ccfc.min/firecracker-ci/v1.10/${ARCH}/vmlinux-5.10.223"
    if curl -fSL --progress-bar "$KERNEL_URL" -o "$KERNEL_DEST"; then
        echo "[✓] Kernel successfully downloaded."
    else
        echo "[!] Fallback: Downloading quickstart kernel..."
        FALLBACK_URL="https://s3.amazonaws.com/spec.ccfc.min/img/quickstart_guide/${ARCH}/kernels/vmlinux.bin"
        curl -fSL --progress-bar "$FALLBACK_URL" -o "$KERNEL_DEST"
        echo "[✓] Fallback kernel downloaded."
    fi
fi

# 2. Download Rootfs if missing
if [ -f "$ROOTFS_DEST" ]; then
    echo "[✓] Rootfs already exists at ${ROOTFS_DEST}"
else
    echo "[*] Downloading Ubuntu ext4 base rootfs..."
    # Firecracker CI Ubuntu rootfs
    ROOTFS_URL="https://s3.amazonaws.com/spec.ccfc.min/firecracker-ci/v1.10/${ARCH}/ubuntu-22.04.ext4"
    if curl -fSL --progress-bar "$ROOTFS_URL" -o "$ROOTFS_DEST"; then
        echo "[✓] Ubuntu 22.04 rootfs downloaded."
    else
        echo "[!] Fallback: Downloading quickstart rootfs..."
        FALLBACK_ROOTFS="https://s3.amazonaws.com/spec.ccfc.min/img/quickstart_guide/${ARCH}/rootfs/bionic.rootfs.ext4"
        curl -fSL --progress-bar "$FALLBACK_ROOTFS" -o "$ROOTFS_DEST"
        echo "[✓] Fallback rootfs downloaded."
    fi
fi

# 3. Ensure permissions
[ -f "$KERNEL_DEST" ] && chmod 644 "$KERNEL_DEST" || true
[ -f "$ROOTFS_DEST" ] && chmod 644 "$ROOTFS_DEST" || true

echo ""
echo "[✓] Assets ready! You can now start CryoSpawn."
