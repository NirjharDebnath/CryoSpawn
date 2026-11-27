# 💻 Web Console (`xterm.js`)

CryoSpawn provides a full interactive Linux terminal directly inside the browser—no local `ssh` client, `ProxyCommand`, or open SSH ports (`22`/`2200`) required. It works seamlessly over LAN and through **Cloudflare Tunnels + Nginx** (`wss://cryospawn.<domain>/ws/console/<uuid>`).

---

## Architecture & Data Flow

```text
[ Browser: xterm.js + FitAddon ]
        │  WebSocket (ws:// or wss://)
        │  Path: /ws/console/vm-da05a06c
        │  Auth: Flask session cookie
        ▼
[ Nginx (:80) ]  (Upgrade + Connection "upgrade" headers)
        │
        ▼
[ Flask Frontend (frontend/app.py :8080) ]
        │  1. Verifies session['user_id']
        │  2. Validates UUID regex: ^(?:vm-)?[a-fA-F0-9]{4,64}\Z
        │  3. Queries Daemon GET /api/vms/<uuid> with X-User-ID & X-Role
        │     -> Rejects 403 if caller does not own VM (unless admin)
        │     -> Rejects 400 if ssh_exposed is false
        ▼
[ frontend/ws_console.py ]
        │  1. Completes RFC 6455 handshake (101 Switching Protocols)
        │  2. Spawns `ssh -tt -o ProxyCommand="nc -U /tmp/cryo-<hex>_22.sock" root@localhost` in a PTY
        ▼
[ Ghost Proxy (_22.sock) ] ──► (Wakes VM if hibernated) ──► [ VM sshd (172.16.x.2:22) ]
```

---

## Key Components

### 1. Zero-Dependency RFC 6455 Bridge ([`frontend/ws_console.py`](../frontend/ws_console.py))
* Uses Python's standard library (`pty`, `select`, `termios`, `fcntl`, `socket`, `hashlib`)—no `flask-sock`, `simple-websocket`, or `paramiko` required.
* **Bidirectional framing:** Reads PTY output and pushes UTF-8 WebSocket text frames to `xterm.js`; reads browser JSON messages (`{"type": "input", "data": "..."}` and `{"type": "resize", "cols": N, "rows": M}`) and applies `TIOCSWINSZ` + `SIGWINCH` to the PTY.
* **Authentication:** Uses `ubuntu-24.04.id_rsa` if present in the project root, and falls back seamlessly to interactive password prompts inside `xterm.js` if the guest image uses password login.

### 2. Werkzeug 3.x & Socket Shutdown Details
* **Dual route registration:** Werkzeug 3.x rejects `Upgrade: websocket` requests with `400 WebsocketMismatch` unless the route specifies `websocket=True`. [`ws_console`](../frontend/app.py) registers both `websocket=True` and `websocket=False` so WebSocket upgrades succeed while plain HTTP probes still receive clean `401`/`403`/`426` JSON errors.
* **Clean socket teardown (`shutdown(SHUT_RDWR)`):** Because Werkzeug's `rfile`/`wfile` keep `_io_refs > 0` on the underlying socket, calling `sock.close()` alone leaves the TCP fd open and causes Werkzeug to write `HTTP/1.1 204` bytes onto the WebSocket stream (`Invalid frame header` in Chrome). Calling `sock.shutdown(socket.SHUT_RDWR)` before `sock.close()` prevents any HTTP bytes from leaking onto the WebSocket.

### 3. UI Entry Points
* **In-page Modal:** Click **`>_ Console`** on any VM row with **SSH (Port 22)** enabled in [`internal/ui/project.html`](../internal/ui/project.html) (`running`, `hibernated`, or `stopped`—Ghost Proxy auto-wakes sleeping VMs).
* **Standalone Full-Screen Tab:** Click **Pop out ↗** or visit `/console/<uuid>` ([`internal/ui/console.html`](../internal/ui/console.html)).
