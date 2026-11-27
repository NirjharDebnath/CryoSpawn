"""Daemon lifecycle, host telemetry, emergency cleanup, and global VM proxy routes."""

import os
import shutil
import signal
import socket
import sqlite3
import subprocess
import time

import requests
from flask import Blueprint, Response, jsonify, request, session

from admin.config import (
    DAEMON_LOG_PATH,
    DAEMON_PID_PATH,
    LOG_DIR,
    ROOT_DIR,
    VM_UUID_RE,
    admin_required,
    daemon_headers,
    fix_db_perms,
    get_active_rootfs_path,
    get_backend_port,
    get_backend_url,
    get_daemon_bin,
    get_db_path,
    get_kernel_path,
    get_ssh_key_path,
)
from frontend.ws_console import bridge_websocket_to_pty, build_ws_accept

daemon_bp = Blueprint("admin_daemon", __name__)


def find_daemon_pid():
    if os.path.exists(DAEMON_PID_PATH):
        try:
            with open(DAEMON_PID_PATH, "r", encoding="utf-8") as f:
                pid = int(f.read().strip())
            if pid > 0:
                os.kill(pid, 0)
                return pid
        except (ValueError, OSError):
            pass
    try:
        out = subprocess.check_output(["pgrep", "-x", "cryospawn"], text=True).strip()
        if out:
            return int(out.splitlines()[0])
    except (subprocess.CalledProcessError, ValueError, FileNotFoundError):
        pass
    return None


def is_daemon_http_alive() -> bool:
    try:
        r = requests.get(f"{get_backend_url()}/api/vms?all=true", headers=daemon_headers(), timeout=1.5)
        return r.status_code in (200, 401, 403)
    except requests.exceptions.RequestException:
        return False


@daemon_bp.route("/api/admin/daemon/status", methods=["GET"])
@admin_required
def daemon_status():
    pid = find_daemon_pid()
    http_alive = is_daemon_http_alive()
    return jsonify({
        "running": bool(pid or http_alive),
        "pid": pid,
        "http_alive": http_alive,
        "backend_url": get_backend_url(),
        "binary_exists": os.path.isfile(get_daemon_bin()),
        "active_rootfs": get_active_rootfs_path(),
        "active_rootfs_name": os.path.basename(get_active_rootfs_path()),
        "active_kernel": get_kernel_path(),
        "active_kernel_name": os.path.basename(get_kernel_path()),
        "is_root": os.geteuid() == 0,
    })


@daemon_bp.route("/api/admin/daemon/start", methods=["POST"])
@admin_required
def daemon_start():
    if find_daemon_pid() or is_daemon_http_alive():
        return jsonify({"status": "already_running", "pid": find_daemon_pid()}), 200
    daemon_bin = get_daemon_bin()
    if not os.path.isfile(daemon_bin):
        return jsonify({"error": f"Daemon binary not found at {daemon_bin}"}), 404

    rootfs_path = get_active_rootfs_path()
    kernel_path = get_kernel_path()
    os.makedirs(LOG_DIR, exist_ok=True)
    log_file = open(DAEMON_LOG_PATH, "ab")
    env = os.environ.copy()
    env["CRYOSPAWN_BASE_PATH"] = ROOT_DIR
    env["CRYOSPAWN_DB_PATH"] = get_db_path()
    env["CRYOSPAWN_KERNEL_PATH"] = kernel_path
    env["CRYOSPAWN_ROOTFS_PATH"] = rootfs_path
    env["CRYOSPAWN_BACKEND_PORT"] = str(get_backend_port())

    cmd = [daemon_bin, "--kernel", kernel_path, "--rootfs", rootfs_path]
    stdbuf_bin = shutil.which("stdbuf")
    if stdbuf_bin:
        cmd = [stdbuf_bin, "-oL", "-eL"] + cmd

    proc = subprocess.Popen(
        cmd,
        cwd=ROOT_DIR,
        env=env,
        stdout=log_file,
        stderr=subprocess.STDOUT,
        stdin=subprocess.DEVNULL,
        start_new_session=True,
    )
    log_file.close()
    with open(DAEMON_PID_PATH, "w", encoding="utf-8") as f:
        f.write(str(proc.pid))

    for _ in range(15):
        if is_daemon_http_alive():
            fix_db_perms(get_db_path())
            return jsonify({"status": "started", "pid": proc.pid, "rootfs": rootfs_path}), 200
        time.sleep(0.15)

    return jsonify({"status": "starting", "pid": proc.pid, "rootfs": rootfs_path}), 200


@daemon_bp.route("/api/admin/daemon/stop", methods=["POST"])
@admin_required
def daemon_stop():
    pid = find_daemon_pid()
    if not pid:
        return jsonify({"status": "not_running"}), 200
    try:
        os.kill(pid, signal.SIGTERM)
        for _ in range(20):
            try:
                os.kill(pid, 0)
                time.sleep(0.1)
            except ProcessLookupError:
                break
        else:
            os.kill(pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    if os.path.exists(DAEMON_PID_PATH):
        try:
            os.remove(DAEMON_PID_PATH)
        except OSError:
            pass
    return jsonify({"status": "stopped", "pid": pid})


@daemon_bp.route("/api/admin/daemon/restart", methods=["POST"])
@admin_required
def daemon_restart():
    daemon_stop()
    time.sleep(0.2)
    return daemon_start()


@daemon_bp.route("/api/admin/daemon/logs", methods=["GET"])
@admin_required
def daemon_logs():
    lines = min(max(int(request.args.get("lines", 120)), 10), 1000)
    if not os.path.exists(DAEMON_LOG_PATH):
        return jsonify({"logs": "(No daemon log file yet at logs/cryospawn.log)"})
    with open(DAEMON_LOG_PATH, "r", encoding="utf-8", errors="replace") as f:
        content = f.readlines()[-lines:]
    return jsonify({"logs": "".join(content)})


@daemon_bp.route("/api/admin/telemetry", methods=["GET"])
@admin_required
def host_telemetry():
    mem = {}
    try:
        with open("/proc/meminfo", "r", encoding="utf-8") as f:
            for line in f:
                parts = line.split()
                if len(parts) >= 2:
                    mem[parts[0].rstrip(":")] = int(parts[1])
    except OSError:
        pass

    mem_total_mb = round(mem.get("MemTotal", 0) / 1024, 1)
    mem_avail_mb = round(mem.get("MemAvailable", 0) / 1024, 1)
    load1, load5, load15 = (0.0, 0.0, 0.0)
    try:
        load1, load5, load15 = os.getloadavg()
    except OSError:
        pass

    disk = shutil.disk_usage(ROOT_DIR)
    taps = []
    try:
        out = subprocess.check_output(["ip", "-o", "link", "show"], text=True, stderr=subprocess.DEVNULL)
        for line in out.splitlines():
            if "cryo-" in line:
                taps.append(line.split(": ")[1].split("@")[0])
    except Exception:
        pass

    sockets = sorted(f for f in os.listdir("/tmp") if f.startswith("cryo-") and (f.endswith(".sock") or f.endswith(".socket")))
    return jsonify({
        "is_root": os.geteuid() == 0,
        "load_avg": [round(load1, 2), round(load5, 2), round(load15, 2)],
        "mem_total_mb": mem_total_mb,
        "mem_used_mb": round(mem_total_mb - mem_avail_mb, 1),
        "disk_total_gb": round(disk.total / (1024 ** 3), 2),
        "disk_free_gb": round(disk.free / (1024 ** 3), 2),
        "taps": taps,
        "sockets": sockets,
    })


@daemon_bp.route("/api/admin/emergency/hibernate-all", methods=["POST"])
@admin_required
def emergency_hibernate_all():
    try:
        r = requests.get(f"{get_backend_url()}/api/vms?all=true", headers=daemon_headers(), timeout=5)
        if r.status_code != 200:
            return jsonify({"error": "Daemon returned non-200"}), 502
        vms = r.json()
    except requests.exceptions.RequestException as e:
        return jsonify({"error": f"Daemon unavailable: {e}"}), 502

    results = []
    for vm in vms:
        if vm.get("status") == "running":
            uuid = vm.get("uuid")
            try:
                hr = requests.post(
                    f"{get_backend_url()}/api/vms/{uuid}/hibernate?all=true",
                    headers=daemon_headers(),
                    timeout=10,
                )
                results.append({"uuid": uuid, "ok": hr.status_code == 200})
            except requests.exceptions.RequestException:
                results.append({"uuid": uuid, "ok": False})
    return jsonify({"hibernated": results})


@daemon_bp.route("/api/admin/emergency/cleanup-orphans", methods=["POST"])
@admin_required
def cleanup_orphans():
    removed_taps = []
    removed_sockets = []
    active_hexes = set()
    db_path = get_db_path()
    if os.path.exists(db_path):
        conn = sqlite3.connect(db_path, timeout=10)
        try:
            c = conn.cursor()
            c.execute("SELECT uuid FROM vms")
            for (u,) in c.fetchall():
                if u:
                    active_hexes.add(u[3:] if u.startswith("vm-") else u)
        except sqlite3.Error:
            pass
        finally:
            conn.close()

    for fname in os.listdir("/tmp"):
        if fname.startswith("cryo-") and (fname.endswith(".sock") or fname.endswith(".socket")):
            stem = fname[5:].split(".")[0].split("_")[0]
            if stem not in active_hexes:
                try:
                    os.unlink(os.path.join("/tmp", fname))
                    removed_sockets.append(fname)
                except OSError:
                    pass

    if os.geteuid() == 0:
        try:
            out = subprocess.check_output(["ip", "-o", "link", "show"], text=True, stderr=subprocess.DEVNULL)
            for line in out.splitlines():
                if "cryo-" in line:
                    tap = line.split(": ")[1].split("@")[0]
                    stem = tap[5:]
                    if stem not in active_hexes:
                        subprocess.run(["ip", "link", "set", "dev", tap, "down"], check=False, stderr=subprocess.DEVNULL)
                        res = subprocess.run(["ip", "link", "delete", tap, "type", "tun"], check=False, stderr=subprocess.DEVNULL)
                        if res.returncode != 0:
                            subprocess.run(["ip", "link", "delete", "dev", tap], check=False, stderr=subprocess.DEVNULL)
                        removed_taps.append(tap)
        except Exception:
            pass

    return jsonify({"removed_taps": removed_taps, "removed_sockets": removed_sockets})


@daemon_bp.route("/api/admin/daemon-api/<path:path>", methods=["GET", "POST", "PUT", "PATCH", "DELETE"])
@admin_required
def proxy_daemon_api(path):
    url = f"{get_backend_url()}/api/{path}"
    qs = request.query_string.decode("utf-8", errors="replace")
    if "all=" not in qs:
        qs = f"{qs}&all=true" if qs else "all=true"
    url = f"{url}?{qs}"

    headers = daemon_headers()
    if request.content_type:
        headers["Content-Type"] = request.content_type

    try:
        resp = requests.request(
            method=request.method,
            url=url,
            headers=headers,
            data=request.get_data(),
            allow_redirects=False,
            timeout=15,
        )
        excluded = {"content-encoding", "content-length", "transfer-encoding", "connection"}
        resp_headers = [(k, v) for k, v in resp.headers.items() if k.lower() not in excluded]
        return Response(resp.content, status=resp.status_code, headers=resp_headers)
    except requests.exceptions.RequestException as e:
        return jsonify({"error": f"C++ daemon unreachable: {e}"}), 502


@daemon_bp.route("/ws/admin/console/<vm_uuid>", websocket=True)
@daemon_bp.route("/ws/admin/console/<vm_uuid>", websocket=False)
def ws_admin_vm_console(vm_uuid):
    if not session.get("admin_authenticated"):
        return jsonify({"error": "Unauthorized"}), 401
    if not VM_UUID_RE.match(vm_uuid):
        return jsonify({"error": "Invalid VM UUID"}), 400
    upgrade = request.headers.get("Upgrade", "")
    ws_key = request.headers.get("Sec-WebSocket-Key", "")
    if upgrade.lower() != "websocket" or not ws_key:
        return jsonify({"error": "WebSocket upgrade required"}), 426
    sock = request.environ.get("werkzeug.socket") or request.environ.get("gunicorn.socket")
    if sock is None:
        return jsonify({"error": "WebSocket transport unavailable"}), 501

    hex_id = vm_uuid[3:] if vm_uuid.startswith("vm-") else vm_uuid
    uds_path = f"/tmp/cryo-{hex_id}_22.sock"
    handshake = (
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        f"Sec-WebSocket-Accept: {build_ws_accept(ws_key)}\r\n\r\n"
    ).encode("ascii")
    try:
        sock.sendall(handshake)
        bridge_websocket_to_pty(sock, uds_path, key_path=get_ssh_key_path())
    finally:
        for fn in (lambda: sock.shutdown(socket.SHUT_RDWR), sock.close):
            try:
                fn()
            except OSError:
                pass
    return Response("", status=204)
