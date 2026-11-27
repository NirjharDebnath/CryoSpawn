# 🐞 Known Issues & Architectural Status

## ✅ Resolved Issues

| # | Issue | Resolution | Where |
| :-: | :--- | :--- | :--- |
| 1 | Hard-coded absolute paths | Dynamic `/proc/self/exe` discovery + `CRYOSPAWN_*` env vars + CLI flags (`--kernel`, `--rootfs`, `--port`) | `internal/config.hpp`, `internal/main.cpp` |
| 2 | Multi-tenant isolation & auth | Session auth in `frontend/app.py`, `X-User-ID` / `X-Role` / `X-Internal-Secret` enforcement across all daemon routes | `frontend/app.py`, `internal/server.hpp` |
| 3 | Ghost Proxy concurrency & `SIGPIPE` | Half-close (`SHUT_WR`), `MSG_NOSIGNAL`, `SIGPIPE` ignore, connection cap (`MAX_PROXY_CONNECTIONS`) | `internal/proxy.hpp` |
| 4 | Nginx socket visibility & WebSockets | Added `Upgrade` / `Connection "upgrade"` headers and systemd `PrivateTmp=false` override | `nginx/server.sh` |
| 5 | Werkzeug WebSocket `400` & `Invalid frame header` | Registered both `websocket=True` and `websocket=False`; added `sock.shutdown(SHUT_RDWR)` before `sock.close()` | `frontend/app.py`, `admin/daemon_routes.py`, `admin/rootfs_routes.py` |
| 6 | Ubuntu 24.04 SSH hang on banner | Switched `ssh.socket` → `ssh.service` and paired Ubuntu 24.04 images with `vmlinux-6.18.51` | `admin/rootfs_routes.py`, `utils/setup_guest_devtools.sh` |
| 7 | Daemon log buffering & TAP cleanup | Spawned daemon under `stdbuf -oL -eL`; updated TAP cleanup to `ip link delete <tap> type tun` | `admin/daemon_routes.py` |

---

## ⚠️ Current Limitations & Operational Notes

1. **Custom `/api/vms/:uuid/proxy` `host_port` persistence:**
   Custom TCP port mappings created via `POST /api/vms/:uuid/proxy` are kept in memory only and do not persist across daemon restarts (nor is there a UI form for custom TCP ports yet; HTTP/HTTPS apps should use port `80` through Nginx).
2. **Ghost Proxy `FD_CLOEXEC`:**
   An orphaned `firecracker` child process can hold onto a Ghost Proxy TCP port (such as `:2200`) if the daemon terminates unexpectedly, because Ghost Proxy sockets are not yet opened with `SOCK_CLOEXEC`.
3. **Default `admin` auto-recreation:**
   In [`frontend/app.py`](../frontend/app.py), `init_db()` auto-creates `admin` / `admin123` whenever the `users` table has `0` rows. Always create at least one non-default user account before deleting `admin`.
4. **Kernel ↔ RootFS ABI pairing:**
   `ubuntu-24.04-minimal*.ext4` requires `vmlinux-6.18.51`, while legacy `ubuntu-rootfs.ext4` requires `ubuntu-vmlinux.bin` (`4.14.174`). Always restart the daemon (and recreate/restart VMs) after switching kernels.
5. **Flask development server:**
   `frontend/app.py` and `admin/app.py` use Werkzeug's threaded development server (`threaded=True`). This works well for homelab use, while high-concurrency production deployments should front them with a production WSGI/ASGI server.
