"""Zero-dependency RFC 6455 WebSocket + PTY SSH bridge for CryoSpawn xterm.js console."""

import base64
import errno
import fcntl
import hashlib
import json
import os
import pty
import select
import signal
import socket
import struct
import termios

WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

OPCODE_CONT = 0x0
OPCODE_TEXT = 0x1
OPCODE_BINARY = 0x2
OPCODE_CLOSE = 0x8
OPCODE_PING = 0x9
OPCODE_PONG = 0xA


def build_ws_accept(key: str) -> str:
    digest = hashlib.sha1((key.strip() + WS_GUID).encode("utf-8")).digest()
    return base64.b64encode(digest).decode("ascii")


def send_ws_frame(sock: socket.socket, opcode: int, payload: bytes) -> None:
    length = len(payload)
    header = bytearray([0x80 | (opcode & 0x0F)])
    if length < 126:
        header.append(length)
    elif length < 65536:
        header.append(126)
        header.extend(struct.pack("!H", length))
    else:
        header.append(127)
        header.extend(struct.pack("!Q", length))
    sock.sendall(bytes(header) + payload)


def send_ws_text(sock: socket.socket, text: str) -> None:
    send_ws_frame(sock, OPCODE_TEXT, text.encode("utf-8", errors="replace"))


def _recv_exact(sock: socket.socket, n: int) -> bytes:
    buf = bytearray()
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise EOFError("WebSocket closed")
        buf.extend(chunk)
    return bytes(buf)


def read_ws_frame(sock: socket.socket):
    """Read a single RFC 6455 frame. Returns (opcode, payload_bytes)."""
    b1, b2 = _recv_exact(sock, 2)
    opcode = b1 & 0x0F
    masked = (b2 & 0x80) != 0
    length = b2 & 0x7F

    if length == 126:
        (length,) = struct.unpack("!H", _recv_exact(sock, 2))
    elif length == 127:
        (length,) = struct.unpack("!Q", _recv_exact(sock, 8))

    if length > 1024 * 1024:
        raise ValueError("WebSocket frame too large")

    mask_key = _recv_exact(sock, 4) if masked else b""
    payload = _recv_exact(sock, length) if length else b""

    if masked and payload:
        payload = bytes(b ^ mask_key[i % 4] for i, b in enumerate(payload))

    return opcode, payload


def set_pty_winsize(fd: int, rows: int, cols: int) -> None:
    rows = max(1, min(int(rows), 500))
    cols = max(1, min(int(cols), 500))
    winsize = struct.pack("HHHH", rows, cols, 0, 0)
    fcntl.ioctl(fd, termios.TIOCSWINSZ, winsize)


def spawn_ssh_pty(uds_path: str, key_path: str = "", rows: int = 24, cols: int = 80):
    """Spawn an interactive SSH session over the VM's Ghost Proxy Unix socket in a PTY."""
    master_fd, slave_fd = pty.openpty()
    set_pty_winsize(master_fd, rows, cols)

    cmd = [
        "ssh",
        "-tt",
        "-o", "StrictHostKeyChecking=no",
        "-o", "UserKnownHostsFile=/dev/null",
        "-o", "LogLevel=ERROR",
        "-o", "ConnectTimeout=15",
        "-o", f"ProxyCommand=nc -U {uds_path}",
    ]
    if key_path and os.path.isfile(key_path):
        cmd.extend(["-i", key_path, "-o", "IdentitiesOnly=no"])
    cmd.append("root@localhost")

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
            os.execvp("ssh", cmd)
        except Exception:
            os._exit(127)

    os.close(slave_fd)
    return pid, master_fd


def bridge_websocket_to_pty(sock: socket.socket, uds_path: str, key_path: str = "") -> None:
    """Bridge an accepted RFC 6455 WebSocket connection to an SSH PTY session."""
    pid, master_fd = spawn_ssh_pty(uds_path, key_path=key_path)
    sock.setblocking(True)

    try:
        while True:
            rlist, _, _ = select.select([sock, master_fd], [], [], 30.0)
            if not rlist:
                # Keepalive ping
                send_ws_frame(sock, OPCODE_PING, b"ping")
                continue

            if master_fd in rlist:
                try:
                    data = os.read(master_fd, 4096)
                except OSError as e:
                    if e.errno in (errno.EIO, errno.EBADF):
                        break
                    raise
                if not data:
                    break
                send_ws_text(sock, data.decode("utf-8", errors="replace"))

            if sock in rlist:
                opcode, payload = read_ws_frame(sock)
                if opcode == OPCODE_CLOSE:
                    try:
                        send_ws_frame(sock, OPCODE_CLOSE, b"")
                    except OSError:
                        pass
                    break
                elif opcode == OPCODE_PING:
                    send_ws_frame(sock, OPCODE_PONG, payload)
                elif opcode == OPCODE_PONG:
                    continue
                elif opcode in (OPCODE_TEXT, OPCODE_BINARY, OPCODE_CONT):
                    text = payload.decode("utf-8", errors="replace")
                    if text.startswith("{"):
                        try:
                            msg = json.loads(text)
                            if isinstance(msg, dict) and msg.get("type") == "resize":
                                rows = int(msg.get("rows", 24))
                                cols = int(msg.get("cols", 80))
                                set_pty_winsize(master_fd, rows, cols)
                                try:
                                    os.kill(pid, signal.SIGWINCH)
                                except ProcessLookupError:
                                    pass
                                continue
                            if isinstance(msg, dict) and msg.get("type") == "input":
                                data_str = msg.get("data", "")
                                if isinstance(data_str, str) and data_str:
                                    os.write(master_fd, data_str.encode("utf-8"))
                                continue
                        except (ValueError, TypeError):
                            pass
                    if payload:
                        os.write(master_fd, payload)
    except (EOFError, OSError, ValueError):
        pass
    finally:
        try:
            os.close(master_fd)
        except OSError:
            pass
        try:
            os.kill(pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
        try:
            waited_pid, _ = os.waitpid(pid, os.WNOHANG)
            if waited_pid == 0:
                os.kill(pid, signal.SIGKILL)
                os.waitpid(pid, 0)
        except ChildProcessError:
            pass
