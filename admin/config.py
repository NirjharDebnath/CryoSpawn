"""Shared configuration, auth helpers, and path resolvers for CryoSpawn Admin."""

import os
import re
import secrets
from functools import wraps

from flask import jsonify, session

ROOT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
LOG_DIR = os.path.join(ROOT_DIR, "logs")
DAEMON_LOG_PATH = os.path.join(LOG_DIR, "cryospawn.log")
DAEMON_PID_PATH = "/tmp/cryospawn_daemon.pid"

VM_UUID_RE = re.compile(r"^(?:vm-)?[a-fA-F0-9]{4,64}\Z")
SQLITE_TABLE_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*\Z")
ROOTFS_NAME_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,63}\.ext4\Z")

_BOOT_FALLBACK_PASSWORD = None


def get_backend_port() -> int:
    return int(os.environ.get("CRYOSPAWN_BACKEND_PORT", "9090"))


def get_backend_url() -> str:
    return os.environ.get("CRYOSPAWN_BACKEND_URL", f"http://127.0.0.1:{get_backend_port()}")


def get_db_path() -> str:
    return os.environ.get("CRYOSPAWN_DB_PATH", os.path.join(ROOT_DIR, "cryospawn.db"))


def get_daemon_bin() -> str:
    return os.environ.get("CRYOSPAWN_DAEMON_BIN", os.path.join(ROOT_DIR, "internal", "cryospawn"))


def get_kernel_dir() -> str:
    env_k = os.environ.get("CRYOSPAWN_KERNEL_PATH")
    if env_k:
        return os.path.dirname(os.path.abspath(env_k))
    return os.path.join(ROOT_DIR, "vmlinux")


def _active_kernel_marker() -> str:
    return os.path.join(get_kernel_dir(), ".active_kernel")


def get_kernel_path() -> str:
    env_k = os.environ.get("CRYOSPAWN_KERNEL_PATH")
    if env_k:
        return os.path.abspath(env_k)
    marker = _active_kernel_marker()
    if os.path.isfile(marker):
        try:
            with open(marker, "r", encoding="utf-8") as f:
                name = f.read().strip()
            if name and "/" not in name and ".." not in name:
                candidate = os.path.join(get_kernel_dir(), name)
                if os.path.isfile(candidate):
                    return candidate
        except OSError:
            pass
    return os.path.join(get_kernel_dir(), "ubuntu-vmlinux.bin")


def set_active_kernel_name(name: str) -> str:
    if not name or "/" in name or "\\" in name or ".." in name:
        raise ValueError("Invalid kernel filename")
    target = os.path.join(get_kernel_dir(), name)
    if not os.path.isfile(target):
        raise FileNotFoundError(f"Kernel not found: {name}")
    with open(_active_kernel_marker(), "w", encoding="utf-8") as f:
        f.write(name)
    return target


def get_ssh_key_path() -> str:
    return os.environ.get("CRYOSPAWN_SSH_KEY_PATH", os.path.join(ROOT_DIR, "ubuntu-24.04.id_rsa"))


def get_rootfs_dir() -> str:
    env_rf = os.environ.get("CRYOSPAWN_ROOTFS_PATH")
    if env_rf:
        return os.path.dirname(os.path.abspath(env_rf))
    return os.path.join(ROOT_DIR, "rootfs")


def _active_marker_path() -> str:
    return os.path.join(get_rootfs_dir(), ".active_rootfs")


def get_active_rootfs_path() -> str:
    env_rf = os.environ.get("CRYOSPAWN_ROOTFS_PATH")
    if env_rf:
        return os.path.abspath(env_rf)
    marker = _active_marker_path()
    if os.path.isfile(marker):
        try:
            with open(marker, "r", encoding="utf-8") as f:
                name = f.read().strip()
            if ROOTFS_NAME_RE.match(name):
                candidate = os.path.join(get_rootfs_dir(), name)
                if os.path.isfile(candidate):
                    return candidate
        except OSError:
            pass
    return os.path.join(get_rootfs_dir(), "ubuntu-rootfs.ext4")


def set_active_rootfs_name(name: str) -> str:
    if not ROOTFS_NAME_RE.match(name):
        raise ValueError("Invalid rootfs filename (must end in .ext4)")
    target = os.path.join(get_rootfs_dir(), name)
    if not os.path.isfile(target):
        raise FileNotFoundError(f"RootFS not found: {name}")
    with open(_active_marker_path(), "w", encoding="utf-8") as f:
        f.write(name)
    return target


def fix_db_perms(db_path: str) -> None:
    for ext in ("", "-wal", "-shm"):
        p = db_path + ext
        if os.path.exists(p):
            try:
                os.chmod(p, 0o666)
            except OSError:
                pass


def get_fallback_password() -> str:
    global _BOOT_FALLBACK_PASSWORD
    env_pw = os.environ.get("CRYOSPAWN_ADMIN_PASSWORD", "")
    if env_pw:
        return env_pw
    if _BOOT_FALLBACK_PASSWORD is None:
        _BOOT_FALLBACK_PASSWORD = secrets.token_urlsafe(12)
    return _BOOT_FALLBACK_PASSWORD


def daemon_headers() -> dict:
    headers = {"X-User-ID": "1", "X-Role": "admin"}
    sec = os.environ.get("CRYOSPAWN_INTERNAL_SECRET", "")
    if sec:
        headers["X-Internal-Secret"] = sec
    return headers


def admin_required(fn):
    @wraps(fn)
    def wrapper(*args, **kwargs):
        if not session.get("admin_authenticated"):
            return jsonify({"error": "Admin authentication required"}), 401
        return fn(*args, **kwargs)

    return wrapper
