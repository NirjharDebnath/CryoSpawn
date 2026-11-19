#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

echo "[*] Compiling CryoSpawn daemon..."
g++ -std=c++17 "${ROOT_DIR}/internal/main.cpp" -o "${ROOT_DIR}/internal/cryospawn" -pthread -lsqlite3

echo "[+] Compilation successful: ${ROOT_DIR}/internal/cryospawn"
if [ "${1:-}" == "--run" ] || [ "${1:-}" == "-r" ]; then
    echo "[*] Launching CryoSpawn daemon (requires root)..."
    exec sudo "${ROOT_DIR}/internal/cryospawn"
fi