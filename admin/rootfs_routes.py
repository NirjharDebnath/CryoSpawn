"""Multi-RootFS management, cloning, DNS fixing, resizing, and chroot shell routes."""

import fcntl
import json
import os
import pty
import select
import shutil
import signal
import socket
import subprocess
import termios

from flask import Blueprint, Response, jsonify, request, session

from admin.config import (
    ROOTFS_NAME_RE,
    admin_required,
    get_active_rootfs_path,
    get_kernel_dir,
    get_kernel_path,
    get_rootfs_dir,
    set_active_kernel_name,
    set_active_rootfs_name,
)
from frontend.ws_console import (
    OPCODE_BINARY,
    OPCODE_CLOSE,
    OPCODE_CONT,
    OPCODE_PING,
    OPCODE_PONG,
    OPCODE_TEXT,
    build_ws_accept,
    read_ws_frame,
    send_ws_frame,
    send_ws_text,
    set_pty_winsize,
)

rootfs_bp = Blueprint("admin_rootfs", __name__)


def _mount_dir() -> str:
    return "/tmp/cryospawn_rootfs_mnt"


def _resolve_image(name: str) -> str:
    if not isinstance(name, str) or not ROOTFS_NAME_RE.match(name):
        raise ValueError("Invalid image name (must match [A-Za-z0-9._-]+.ext4)")
    return os.path.join(get_rootfs_dir(), name)


def _list_images() -> list:
    rdir = get_rootfs_dir()
    active_path = get_active_rootfs_path()
    images = []
    if not os.path.isdir(rdir):
        return images
    for fname in sorted(os.listdir(rdir)):
        if not fname.endswith(".ext4") or not ROOTFS_NAME_RE.match(fname):
            continue
        full = os.path.join(rdir, fname)
        if not os.path.isfile(full):
            continue
        st = os.stat(full)
        images.append({
            "name": fname,
            "path": full,
            "size_mb": round(st.st_size / (1024 * 1024), 1),
            "mtime": int(st.st_mtime),
            "active": os.path.abspath(full) == os.path.abspath(active_path),
            "has_backup": os.path.isfile(full + ".bak"),
        })
    return images


@rootfs_bp.route("/api/admin/kernel/list", methods=["GET"])
@admin_required
def kernel_list():
    kdir = get_kernel_dir()
    active_path = get_kernel_path()
    kernels = []
    if os.path.isdir(kdir):
        for fname in sorted(os.listdir(kdir)):
            if fname.startswith("."):
                continue
            full = os.path.join(kdir, fname)
            if not os.path.isfile(full):
                continue
            st = os.stat(full)
            kernels.append({
                "name": fname,
                "path": full,
                "size_mb": round(st.st_size / (1024 * 1024), 1),
                "active": os.path.abspath(full) == os.path.abspath(active_path),
            })
    return jsonify({
        "active_name": os.path.basename(active_path),
        "active_path": active_path,
        "kernels": kernels,
    })


@rootfs_bp.route("/api/admin/kernel/select", methods=["POST"])
@admin_required
def kernel_select():
    data = request.get_json(silent=True) or {}
    name = (data.get("name") or "").strip()
    try:
        path = set_active_kernel_name(name)
        return jsonify({"status": "selected", "name": name, "path": path})
    except ValueError as e:
        return jsonify({"error": str(e)}), 400
    except FileNotFoundError as e:
        return jsonify({"error": str(e)}), 404


@rootfs_bp.route("/api/admin/rootfs/list", methods=["GET"])
@admin_required
def rootfs_list():
    return jsonify({
        "rootfs_dir": get_rootfs_dir(),
        "active_path": get_active_rootfs_path(),
        "active_name": os.path.basename(get_active_rootfs_path()),
        "images": _list_images(),
        "is_root": os.geteuid() == 0,
    })


@rootfs_bp.route("/api/admin/rootfs/select", methods=["POST"])
@admin_required
def rootfs_select():
    data = request.get_json(silent=True) or {}
    name = (data.get("name") or "").strip()
    try:
        path = set_active_rootfs_name(name)
        return jsonify({"status": "selected", "name": name, "path": path})
    except ValueError as e:
        return jsonify({"error": str(e)}), 400
    except FileNotFoundError as e:
        return jsonify({"error": str(e)}), 404


@rootfs_bp.route("/api/admin/rootfs/clone", methods=["POST"])
@admin_required
def rootfs_clone():
    data = request.get_json(silent=True) or {}
    src_name = (data.get("source") or "").strip()
    dst_name = (data.get("target") or "").strip()
    if dst_name and not dst_name.endswith(".ext4"):
        dst_name += ".ext4"
    try:
        src_path = _resolve_image(src_name)
        dst_path = _resolve_image(dst_name)
    except ValueError as e:
        return jsonify({"error": str(e)}), 400

    if not os.path.isfile(src_path):
        return jsonify({"error": f"Source image '{src_name}' not found"}), 404
    if os.path.exists(dst_path):
        return jsonify({"error": f"Target image '{dst_name}' already exists"}), 409

    shutil.copy2(src_path, dst_path)
    os.chmod(dst_path, 0o644)
    return jsonify({"status": "cloned", "name": dst_name, "path": dst_path}), 201


@rootfs_bp.route("/api/admin/rootfs/delete", methods=["POST"])
@admin_required
def rootfs_delete():
    data = request.get_json(silent=True) or {}
    name = (data.get("name") or "").strip()
    try:
        target = _resolve_image(name)
    except ValueError as e:
        return jsonify({"error": str(e)}), 400
    if not os.path.isfile(target):
        return jsonify({"error": "Image not found"}), 404
    if os.path.abspath(target) == os.path.abspath(get_active_rootfs_path()):
        return jsonify({"error": "Cannot delete the currently active RootFS. Switch to another first."}), 400
    os.remove(target)
    return jsonify({"status": "deleted", "name": name})


@rootfs_bp.route("/api/admin/rootfs/fix-dns", methods=["POST"])
@admin_required
def rootfs_fix_dns():
    if os.geteuid() != 0:
        return jsonify({"error": "Must run admin panel with sudo (root) to loop-mount rootfs"}), 403
    data = request.get_json(silent=True) or {}
    name = (data.get("name") or "").strip()
    try:
        target = _resolve_image(name) if name else get_active_rootfs_path()
    except ValueError as e:
        return jsonify({"error": str(e)}), 400
    if not os.path.isfile(target):
        return jsonify({"error": "RootFS image not found"}), 404

    mnt = _mount_dir()
    os.makedirs(mnt, exist_ok=True)
    subprocess.run(["umount", "-l", mnt], check=False, stderr=subprocess.DEVNULL)
    try:
        subprocess.run(["mount", "-o", "loop", target, mnt], check=True)
        resolv = os.path.join(mnt, "etc", "resolv.conf")
        if os.path.islink(resolv):
            os.unlink(resolv)
        with open(resolv, "w", encoding="utf-8") as f:
            f.write("nameserver 1.1.1.1\nnameserver 8.8.8.8\n")
        os.chmod(resolv, 0o644)
        return jsonify({"status": "dns_updated", "target": target})
    except subprocess.CalledProcessError as e:
        return jsonify({"error": f"Mount failed: {e}"}), 500
    finally:
        subprocess.run(["umount", mnt], check=False, stderr=subprocess.DEVNULL)


@rootfs_bp.route("/api/admin/rootfs/resize", methods=["POST"])
@admin_required
def rootfs_resize():
    data = request.get_json(silent=True) or {}
    name = (data.get("name") or "").strip()
    size_gb = int(data.get("size_gb", 0))
    if size_gb < 1 or size_gb > 50:
        return jsonify({"error": "size_gb must be between 1 and 50"}), 400
    try:
        target = _resolve_image(name) if name else get_active_rootfs_path()
    except ValueError as e:
        return jsonify({"error": str(e)}), 400
    if not os.path.isfile(target):
        return jsonify({"error": "RootFS image not found"}), 404
    try:
        subprocess.run(["truncate", "-s", f"{size_gb}G", target], check=True)
        subprocess.run(["e2fsck", "-fy", target], check=False)
        subprocess.run(["resize2fs", target], check=True)
        return jsonify({"status": "resized", "name": os.path.basename(target), "size_gb": size_gb})
    except subprocess.CalledProcessError as e:
        return jsonify({"error": f"Resize failed: {e}"}), 500


# Backward-compatible info/stage/promote/rollback endpoints
@rootfs_bp.route("/api/admin/rootfs/info", methods=["GET"])
@admin_required
def rootfs_info():
    active = get_active_rootfs_path()
    staging = active + ".staging.ext4"
    backup = active + ".bak"

    def stat_file(path):
        if not os.path.exists(path):
            return {"exists": False, "path": path}
        st = os.stat(path)
        return {"exists": True, "path": path, "size_mb": round(st.st_size / (1024 * 1024), 1), "mtime": int(st.st_mtime)}

    return jsonify({"golden": stat_file(active), "staging": stat_file(staging), "backup": stat_file(backup), "is_root": os.geteuid() == 0})


@rootfs_bp.route("/api/admin/rootfs/stage", methods=["POST"])
@admin_required
def rootfs_stage():
    active = get_active_rootfs_path()
    if not os.path.exists(active):
        return jsonify({"error": f"Active rootfs not found at {active}"}), 404
    staging = active + ".staging.ext4"
    shutil.copy2(active, staging)
    os.chmod(staging, 0o644)
    return jsonify({"status": "staged", "path": staging})


@rootfs_bp.route("/api/admin/rootfs/promote", methods=["POST"])
@admin_required
def rootfs_promote():
    active = get_active_rootfs_path()
    staging = active + ".staging.ext4"
    if not os.path.exists(staging):
        return jsonify({"error": "No staging copy exists"}), 400
    if os.path.exists(active):
        shutil.copy2(active, active + ".bak")
    shutil.move(staging, active)
    os.chmod(active, 0o644)
    return jsonify({"status": "promoted"})


@rootfs_bp.route("/api/admin/rootfs/rollback", methods=["POST"])
@admin_required
def rootfs_rollback():
    active = get_active_rootfs_path()
    bak = active + ".bak"
    if not os.path.exists(bak):
        return jsonify({"error": "No backup rootfs exists"}), 404
    shutil.copy2(bak, active)
    os.chmod(active, 0o644)
    return jsonify({"status": "rolled_back"})


@rootfs_bp.route("/ws/admin/rootfs-shell", websocket=True)
@rootfs_bp.route("/ws/admin/rootfs-shell", websocket=False)
def ws_admin_rootfs_shell():
    if not session.get("admin_authenticated"):
        return jsonify({"error": "Unauthorized"}), 401
    if os.geteuid() != 0:
        return jsonify({"error": "Root privileges required for rootfs chroot"}), 403

    image_name = (request.args.get("image") or "").strip()
    try:
        target = _resolve_image(image_name) if image_name else get_active_rootfs_path()
    except ValueError as e:
        return jsonify({"error": str(e)}), 400
    if not os.path.isfile(target):
        return jsonify({"error": "Target rootfs image not found"}), 404

    upgrade = request.headers.get("Upgrade", "")
    ws_key = request.headers.get("Sec-WebSocket-Key", "")
    if upgrade.lower() != "websocket" or not ws_key:
        return jsonify({"error": "WebSocket upgrade required"}), 426
    sock = request.environ.get("werkzeug.socket") or request.environ.get("gunicorn.socket")
    if sock is None:
        return jsonify({"error": "WebSocket transport unavailable"}), 501

    mnt = _mount_dir()
    os.makedirs(mnt, exist_ok=True)
    handshake = (
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        f"Sec-WebSocket-Accept: {build_ws_accept(ws_key)}\r\n\r\n"
    ).encode("ascii")

    try:
        sock.sendall(handshake)
    except OSError:
        try:
            sock.close()
        except OSError:
            pass
        return Response("", status=204)

    for sub in ("dev", "sys", "proc"):
        subprocess.run(["umount", "-l", os.path.join(mnt, sub)], check=False, stderr=subprocess.DEVNULL)
    subprocess.run(["umount", "-l", mnt], check=False, stderr=subprocess.DEVNULL)

    mount_res = subprocess.run(["mount", "-o", "loop", target, mnt], capture_output=True, text=True)
    if mount_res.returncode != 0:
        try:
            send_ws_text(sock, f"\r\n\x1b[31m[-] Failed to mount {os.path.basename(target)}: {mount_res.stderr.strip()}\x1b[0m\r\n")
            sock.close()
        except OSError:
            pass
        return Response("", status=204)

    for sub in ("proc", "sys", "dev"):
        os.makedirs(os.path.join(mnt, sub), exist_ok=True)
        subprocess.run(["mount", "--bind", f"/{sub}", os.path.join(mnt, sub)], check=False)

    for tmp_dir in (os.path.join(mnt, "tmp"), os.path.join(mnt, "var", "tmp")):
        if os.path.isdir(tmp_dir):
            try:
                os.chmod(tmp_dir, 0o1777)
            except OSError:
                pass

    resolv = os.path.join(mnt, "etc", "resolv.conf")
    if not os.path.exists(resolv) or os.path.getsize(resolv) <= 2:
        if os.path.islink(resolv):
            os.unlink(resolv)
        with open(resolv, "w", encoding="utf-8") as f:
            f.write("nameserver 1.1.1.1\nnameserver 8.8.8.8\n")

    chroot_bin = shutil.which("chroot") or "/usr/sbin/chroot"
    shell_bin = "/bin/bash" if os.path.exists(os.path.join(mnt, "bin", "bash")) else "/bin/sh"

    master_fd, slave_fd = pty.openpty()
    set_pty_winsize(master_fd, 24, 80)
    pid = os.fork()
    if pid == 0:
        try:
            os.close(master_fd)
            os.setsid()
            fcntl.ioctl(slave_fd, termios.TIOCSCTTY, 0)
            os.dup2(slave_fd, 0)
            os.dup2(slave_fd, 1)
            os.dup2(slave_fd, 2)
            if slave_fd > 2:
                os.close(slave_fd)
            os.environ["TERM"] = "xterm-256color"
            os.environ["PATH"] = "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"
            os.execv(chroot_bin, [chroot_bin, mnt, shell_bin])
        except Exception as exc:
            os.write(2, f"chroot exec failed: {exc}\n".encode("utf-8"))
            os._exit(127)
    os.close(slave_fd)

    try:
        sock.setblocking(True)
        while True:
            rlist, _, _ = select.select([sock, master_fd], [], [], 30.0)
            if not rlist:
                send_ws_frame(sock, OPCODE_PING, b"ping")
                continue
            if master_fd in rlist:
                data = os.read(master_fd, 4096)
                if not data:
                    break
                send_ws_text(sock, data.decode("utf-8", errors="replace"))
            if sock in rlist:
                opcode, payload = read_ws_frame(sock)
                if opcode == OPCODE_CLOSE:
                    break
                elif opcode == OPCODE_PING:
                    send_ws_frame(sock, OPCODE_PONG, payload)
                elif opcode in (OPCODE_TEXT, OPCODE_BINARY, OPCODE_CONT):
                    text = payload.decode("utf-8", errors="replace")
                    if text.startswith("{"):
                        try:
                            msg = json.loads(text)
                            if msg.get("type") == "resize":
                                set_pty_winsize(master_fd, int(msg.get("rows", 24)), int(msg.get("cols", 80)))
                                os.kill(pid, signal.SIGWINCH)
                                continue
                            if msg.get("type") == "input":
                                os.write(master_fd, msg.get("data", "").encode("utf-8"))
                                continue
                        except Exception:
                            pass
                    if payload:
                        os.write(master_fd, payload)
    except Exception:
        pass
    finally:
        try:
            os.close(master_fd)
        except OSError:
            pass
        try:
            os.kill(pid, signal.SIGTERM)
            os.waitpid(pid, 0)
        except Exception:
            pass
        for sub in ("dev", "sys", "proc"):
            subprocess.run(["umount", "-l", os.path.join(mnt, sub)], check=False, stderr=subprocess.DEVNULL)
        subprocess.run(["umount", "-l", mnt], check=False, stderr=subprocess.DEVNULL)
        for fn in (lambda: sock.shutdown(socket.SHUT_RDWR), sock.close):
            try:
                fn()
            except OSError:
                pass
    return Response("", status=204)
