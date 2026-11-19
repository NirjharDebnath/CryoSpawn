#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

echo "=========================================="
echo "    Running CryoSpawn Test Suite          "
echo "=========================================="

echo "[1/2] C++ Core & Ghost Proxy Tests..."
g++ -std=c++17 "${ROOT_DIR}/tests/test_cryospawn.cpp" -o "${ROOT_DIR}/tests/test_runner" -pthread -lsqlite3
"${ROOT_DIR}/tests/test_runner"
rm -f "${ROOT_DIR}/tests/test_runner"

echo ""
echo "[2/2] Python Flask Frontend & Auth Tests..."
PYTHONPATH="${ROOT_DIR}/.venv/lib/python3.12/site-packages" python3.12 -m unittest "${ROOT_DIR}/tests/test_frontend_auth.py"

echo ""
echo "=========================================="
echo " [✓] ALL TESTS PASSED SUCCESSFULLY!       "
echo "=========================================="
