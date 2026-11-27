# 🔌 CryoSpawn HTTP & WebSocket API

CryoSpawn exposes three local/private service layers:

| Layer | Address | Role |
| :--- | :--- | :--- |
| **Tenant Frontend** (`frontend/app.py`) | `0.0.0.0:8080` | Session auth, serves `internal/ui/`, proxies `/api/*` with `X-User-ID` + `X-Role`, serves `xterm.js` `/ws/console/<uuid>` |
| **C++ Daemon** (`internal/main.cpp`) | `127.0.0.1:9090` | Pure REST API managing Firecracker MicroVMs, Ghost Proxies, TAP links, and Elastic Volumes |
| **God-Mode Admin** (`admin/app.py`) | `0.0.0.0:9091` | Private host controller for daemon lifecycle, RootFS/Kernel Studio, SQLite explorer, and `?all=true` management |

---

## 🔐 Tenant Authentication (`:8080`)

| Method | Endpoint | Body | Response |
| :--- | :--- | :--- | :--- |
| `POST` | `/api/auth/login` | `{"username": "...", "password": "..."}` | `{"status": "success", "role": "..."}` |
| `POST` | `/api/auth/register` | `{"username": "...", "password": "..."}` | `{"status": "success", "role": "user"}` (`201`) |
| `POST` | `/api/auth/change-password` | `{"current_password": "...", "new_password": "..."}` | `{"status": "success", "message": "..."}` |
| `POST` | `/api/auth/logout` | — | `{"status": "success"}` |
| `GET` | `/api/auth/me` | — | `{"user_id": 1, "username": "...", "role": "...", "public_domain": "..."}` |
| `GET` | `/api/config` | — | `{"public_domain": "..."}` |

---

## 📁 Projects (`:8080`)

| Method | Endpoint | Purpose |
| :--- | :--- | :--- |
| `GET` | `/api/projects` | List owned projects with live VM counts |
| `POST` | `/api/projects` | Create a project (`{"name": "...", "description": "..."}`) |
| `GET` | `/api/projects/:name` | Get project summary |
| `DELETE` | `/api/projects/:name` | Delete an empty project |

---

## 🖥️ MicroVMs (`:8080` → `:9090`)

Every VM endpoint requires a valid session (or `X-User-ID` / `X-Role` / `X-Internal-Secret` headers when calling `:9090` directly). Non-admins can only access their own VMs; admins can pass `?all=true` for global visibility.

| Method | Endpoint | Purpose |
| :--- | :--- | :--- |
| `GET` | `/api/vms` | List VMs (`?all=true` for admin global view) |
| `GET` | `/api/vms/:uuid` | Get single VM details |
| `POST` | `/api/vms` | Create & boot a VM (`name`, `vcpus`, `mem_mib`, `rootfs_gb`, `project`, `expose_ssh`, `expose_http`, `expose_https`, `allow_interproject_routing`) |
| `DELETE` | `/api/vms/:uuid` | Terminate VM and clean up TAP, sockets, disk, and snapshots |
| `POST` | `/api/vms/:uuid/hibernate` | Snapshot RAM & CPU state to disk and stop Firecracker |
| `POST` | `/api/vms/:uuid/wake` | Resume hibernated VM (or cold-boot if `stopped`) |
| `POST` | `/api/vms/:uuid/start` | Alias for `/wake` |
| `POST` | `/api/vms/:uuid/restart` | Reboot VM in place |
| `PATCH` | `/api/vms/:uuid/ports` | Toggle `ssh_exposed`, `http_exposed`, `https_exposed` |
| `PATCH` | `/api/vms/:uuid/routing` | Toggle `allow_interproject_routing` |
| `POST` | `/api/vms/:uuid/proxy` | Start Ghost Proxy on `/tmp/cryo-<hex>_<vm_port>.sock` (and optional `0.0.0.0:<host_port>`) |

---

## 💻 Web Console (`:8080`)

| Protocol / Method | Endpoint | Purpose |
| :--- | :--- | :--- |
| `GET` | `/console/:uuid` | Session-protected standalone `xterm.js` page |
| `GET` (WebSocket) | `/ws/console/:uuid` | Authenticated RFC 6455 WebSocket bridged to PTY SSH via `/tmp/cryo-<hex>_22.sock` |

---

## 🔗 Network Links & 💾 Volumes (`:8080` → `:9090`)

| Method | Endpoint | Purpose |
| :--- | :--- | :--- |
| `GET` | `/api/links` | List active VM-to-VM links |
| `POST` | `/api/links` | Link two VMs (`{"vm1_id": "<uuid>", "vm2_id": "<uuid>"}`) |
| `DELETE` | `/api/links/:uuid1/:uuid2` | Unlink two VMs |
| `GET` | `/api/volumes` | List Elastic Volumes |
| `POST` | `/api/volumes` | Create volume (`{"name": "...", "size_gb": 2}`) |
| `DELETE` | `/api/volumes/:id` | Delete unattached volume |
| `POST` | `/api/volumes/:id/attach` | Attach volume (`{"vm_id": "<uuid>"}`) |
| `POST` | `/api/volumes/:id/detach` | Detach volume |

---

## ⚡ God-Mode Admin API (`:9091`)

All routes require an authenticated `cryospawn_admin_session` cookie (`POST /api/admin/login`).

| Group | Endpoints |
| :--- | :--- |
| **Daemon Control** | `GET /api/admin/daemon/status`, `POST /api/admin/daemon/start\|stop\|restart`, `GET /api/admin/daemon/logs` |
| **Telemetry & Emergency** | `GET /api/admin/telemetry`, `POST /api/admin/emergency/hibernate-all`, `POST /api/admin/emergency/cleanup-orphans` |
| **Global Proxy** | `/api/admin/daemon-api/<path>` (auto-injects `X-Role: admin` & `?all=true`) |
| **Kernel & RootFS Studio** | `GET /api/admin/kernel/list`, `POST /api/admin/kernel/select`, `GET /api/admin/rootfs/list`, `POST /api/admin/rootfs/select\|clone\|delete\|fix-dns\|resize`, `WS /ws/admin/rootfs-shell?image=<name>` |
| **SQLite & Users** | `GET /api/admin/sqlite/tables`, `POST /api/admin/sqlite/query`, `POST /api/admin/users`, `DELETE /api/admin/users/:id` |
