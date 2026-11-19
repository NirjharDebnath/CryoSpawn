# 🔌 CryoSpawn HTTP API

> **Written for:** developers and operators who call CryoSpawn directly (scripts, `curl`, the dashboard frontend).

<<<<<<< HEAD
CryoSpawn has two HTTP layers:

| Layer | Address | Role |
| :--- | :--- | :--- |
| Flask frontend ([frontend/app.py](../frontend/app.py)) | `0.0.0.0:8080` | Login and sessions, serves the dashboard from `internal/ui/`, forwards `/api/*` to the daemon |
| C++ daemon ([internal/main.cpp](../internal/main.cpp)) | `127.0.0.1:9090` | The REST API that manages VMs, links and volumes |

Clients talk to **port 8080**. The daemon on 9090 is local only and trusts the identity headers that Flask adds.

```mermaid
sequenceDiagram
    participant C as Client
    participant F as Flask :8080
    participant D as Daemon :9090
    C->>F: POST /api/auth/login
    F-->>C: session cookie
    C->>F: GET /api/vms (cookie)
    F->>D: GET /api/vms + X-User-ID + X-Role
    D-->>F: JSON
    F-->>C: JSON
```
=======
The daemon serves a JSON REST API on `http://127.0.0.1:9090` (local only, configurable via `CRYOSPAWN_BACKEND_PORT`). The Flask proxy on `0.0.0.0:8080` handles session authentication, serves the web dashboard from `internal/ui/`, and securely forwards `/api/*` requests to the core daemon with injected authentication headers. Routes are defined in [internal/main.cpp](../internal/main.cpp).
>>>>>>> 816acd8 (feat(ui): enhance dashboard and login UI with improved accessibility and user experience)

## 📋 Conventions & Security

- Request and response bodies are JSON (`Content-Type: application/json`).
<<<<<<< HEAD
- Errors return a non-2xx status and, for most of them, a body like `{"error": "..."}`.
- VMs are addressed by **UUID** (`vm-` plus 8 hex characters, e.g. `vm-da05a06c`). The integer `id` in responses is the internal **slot**, used only for IP allocation. See [DESIGN.md](../DESIGN.md#3-database-design-the-systems-memory).
- Flask rejects every `/api/*` call except the login route with `401` when there is no session. If the daemon is down it answers `502`.
- The daemon allows every origin through CORS (`Access-Control-Allow-Origin: *`).

> [!IMPORTANT]
> Expose only port 8080. Port 9090 has no login of its own: it believes whatever `X-User-ID` and `X-Role` header it receives. See [KNOWN-ISSUES.md](KNOWN-ISSUES.md).

## 🔐 Authentication (Flask, port 8080)

Sessions use a cookie, so keep it between calls (`curl -c jar -b jar`).

| Method | Endpoint | Body | Response |
| :--- | :--- | :--- | :--- |
| `POST` | `/api/auth/login` | `{"username": "admin", "password": "..."}` | `{"status": "success", "role": "admin"}`; `401` with `{"error": "Invalid credentials"}` |
| `POST` | `/api/auth/logout` | none | `{"status": "success"}` |
| `GET` | `/api/auth/me` | none | `{"user_id": 1, "username": "admin", "role": "admin"}`; `401` if not logged in |

A first start creates the user `admin` (role `admin`) with the password `admin123` when the `users` table is empty. Change it: see [SETUP.md](SETUP.md#-first-login).

```bash
curl -c jar -X POST http://localhost:8080/api/auth/login \
  -H 'Content-Type: application/json' \
  -d '{"username":"admin","password":"admin123"}'
```

The dashboard pages (`/dashboard`, `/volumes`, `/networks`, `/settings`) redirect to `/login` without a session.
=======
- Errors return a non-2xx status and a body like `{"error": "..."}`.
- CORS allows cross-origin requests and permits `X-User-ID` and `X-Role` headers.
- Identifiers: VM endpoints accept either a full UUID, short hex prefix, or numerical slot ID.
- **Authentication**: Requests must include `X-User-ID` (integer user ID) and `X-Role` (`admin` or `user`). Unauthenticated requests return `401 Unauthorized`. The Flask proxy on port 8080 injects these headers automatically for authenticated web sessions. Non-admin users are restricted to managing their own VMs and volumes.
>>>>>>> 816acd8 (feat(ui): enhance dashboard and login UI with improved accessibility and user experience)

## 🖥️ MicroVMs

| Method | Endpoint | Purpose |
| :--- | :--- | :--- |
<<<<<<< HEAD
| `GET` | `/api/vms` | List VMs (all of them for an admin, only the caller's own otherwise) |
| `POST` | `/api/vms` | Create and boot a VM |
| `DELETE` | `/api/vms/:uuid` | Terminate a VM and remove its resources |
| `POST` | `/api/vms/:uuid/hibernate` | Snapshot to disk and stop the Firecracker process |
| `POST` | `/api/vms/:uuid/wake` | Restore a hibernated VM |
| `POST` | `/api/vms/:uuid/restart` | Reboot the VM in place |
| `PATCH` | `/api/vms/:uuid/ports` | Turn SSH, HTTP and HTTPS exposure on or off |
| `POST` | `/api/vms/:uuid/proxy` | Start a Ghost Proxy for an arbitrary VM port |

Except for `DELETE`, these handlers answer `404` for an unknown UUID and `403` when a non-admin caller does not own the VM.
=======
| `GET` | `/api/vms` | List VMs (filtered by owner unless admin) |
| `POST` | `/api/vms` | Create and boot a VM |
| `DELETE` | `/api/vms/:uuid` | Terminate a VM and remove its resources |
| `POST` | `/api/vms/:uuid/hibernate` | Snapshot to disk and stop the Firecracker process |
| `POST` | `/api/vms/:uuid/wake` | Restore a hibernated VM (or cold boot if stopped) |
| `POST` | `/api/vms/:uuid/start` | Start or resume a microVM |
| `POST` | `/api/vms/:uuid/restart` | Reboot the VM process cleanly |
| `PATCH` | `/api/vms/:uuid/ports` | Turn SSH (22), HTTP (80), and HTTPS (443) exposure on or off |
| `POST` | `/api/vms/:uuid/proxy` | Start a Ghost Proxy for an arbitrary guest port (UDS and optional TCP) |
>>>>>>> 816acd8 (feat(ui): enhance dashboard and login UI with improved accessibility and user experience)

### `GET /api/vms`

Returns an array of VM objects accessible to the calling user.

| Field | Type | Meaning |
| :--- | :--- | :--- |
<<<<<<< HEAD
| `id` | int | Slot (internal index, not an API identifier) |
| `uuid` | string | VM identifier, e.g. `vm-da05a06c` |
| `name` | string | Human-readable name |
| `ip` | string | Guest IP, e.g. `172.16.0.2` |
| `host_ip` | string | Host-side TAP gateway IP |
| `status` | string | `booting`, `running`, `hibernated`, `waking` or `restarting` |
=======
| `id` | int | Slot ID |
| `uuid` | string | Full VM UUID string |
| `name` | string | VM friendly name |
| `ip` | string | Guest IP, e.g. `172.16.0.2` |
| `host_ip` | string | Host-side TAP gateway IP |
| `status` | string | `booting`, `running`, `hibernated`, `waking`, `stopped` |
>>>>>>> 816acd8 (feat(ui): enhance dashboard and login UI with improved accessibility and user experience)
| `vcpus` | int | Virtual CPU count |
| `mem_mib` | int | RAM in MiB |
| `rootfs_gb` | int | Requested root disk size in GB |
| `tap_name` | string | Host TAP device, e.g. `cryo-da05a06c` |
| `ssh_exposed`, `http_exposed`, `https_exposed` | bool | Ghost Proxy exposure flags |
| `host_port` | int | Legacy field |
| `host_lan_ip` | string | LAN IP of the host's active interface |
| `project` | string | Project namespace |
| `owner_id` | int | User ID of the owner |

### `POST /api/vms`

All fields are optional.

| Field | Type | Default | Notes |
| :--- | :--- | :--- | :--- |
<<<<<<< HEAD
| `name` | string | `""` | Display name (the dashboard form requires one) |
| `vcpus` | int | `1` | |
| `mem_mib` | int | `512` | |
=======
| `name` | string | `"vm-<slot>"` | Optional human-readable name |
| `vcpus` | int | `1` | Core count (1-4) |
| `mem_mib` | int | `512` | Memory in MB |
>>>>>>> 816acd8 (feat(ui): enhance dashboard and login UI with improved accessibility and user experience)
| `expose_ssh` | bool | `false` | Starts the port 22 Ghost Proxy |
| `expose_http` | bool | `false` | Starts the port 80 Ghost Proxy |
| `expose_https` | bool | `false` | Starts the port 443 Ghost Proxy |
| `project` | string | `"default"` | VMs can only be linked within one project |
| `rootfs_gb` | int | `3` | Root disk size (minimum 3 GB for Ubuntu) |

```bash
curl -b jar -X POST http://localhost:8080/api/vms \
  -H 'Content-Type: application/json' \
<<<<<<< HEAD
  -d '{"name":"web-1","vcpus":2,"mem_mib":1024,"expose_http":true,"project":"lab1","rootfs_gb":5}'
```

Response: `{"id": 0, "uuid": "vm-da05a06c", "name": "web-1", "ip": "172.16.0.2", "status": "booting"}`. Booting continues in the background, so poll `GET /api/vms` until `status` is `running`.

Errors: `400` for invalid JSON, `401` without the identity headers, `500` if creation fails.

### `DELETE /api/vms/:uuid`

Response: `{"status": "deleted"}`; `404` for an unknown UUID.

### `POST /api/vms/:uuid/hibernate` and `/wake`
=======
  -d '{"name":"api-worker","vcpus":2,"mem_mib":1024,"expose_http":true,"project":"production","rootfs_gb":5}'
```

Response: `{"id": 0, "uuid": "...", "ip": "172.16.0.2", "status": "booting"}`. Booting continues in the background; poll `GET /api/vms` until `status` is `running`.

### `DELETE /api/vms/:uuid`

Shuts down the VM, stops active ghost proxies, purges snapshot files, releases TAP devices and IP allocations, and clears attached volume mappings.
Response: `{"status": "deleted"}`.

### `POST /api/vms/:uuid/hibernate`, `/wake`, and `/start`
>>>>>>> 816acd8 (feat(ui): enhance dashboard and login UI with improved accessibility and user experience)

- `/hibernate`: Snapshots guest memory and device state to disk and halts the process. Response: `{"status": "hibernated"}`.
- `/wake` / `/start`: Resumes the Firecracker instance from its snapshot in ~5ms. If the VM was stopped (e.g. after attaching/detaching a volume) or if snapshot files are missing, it safely performs a clean boot. Response: `{"status": "running"}`.
Waking or cold-booting also happens transparently upon incoming traffic through any exposed Ghost Proxy socket or port.

### `POST /api/vms/:uuid/restart`

<<<<<<< HEAD
Reboots the VM in place (same TAP, disk and UUID). Response: `{"status": "restarting"}`.

### `PATCH /api/vms/:uuid/ports`

Body fields `ssh_exposed`, `http_exposed`, `https_exposed` (bool, each defaults to `false`). Proxies are started or stopped to match. Response: `{"status": "ports updated"}`.

### `POST /api/vms/:uuid/proxy`

Body: `{"host_port": <int>, "vm_port": <int>}`. Only `vm_port` is used: it starts a Ghost Proxy on `/tmp/cryo_vm_<slot>_<vm_port>.sock` forwarding to that port on the VM. Response: `{"status": "proxy_started", "uds": "<socket path>"}`. Errors: `400` invalid request.

> [!NOTE]
> This socket name uses the numeric slot, unlike the exposure sockets (`/tmp/cryo-<hex>_22.sock`, `_80.sock`, `_443.sock`) that `POST /api/vms` and `PATCH .../ports` create. Here `<hex>` is the UUID without the `vm-` prefix.
=======
Cleanly terminates the existing Firecracker process and relaunches the VM with its current specifications.
Response: `{"status": "restarting"}`.

### `PATCH /api/vms/:uuid/ports`

Body fields: `ssh_exposed`, `http_exposed`, `https_exposed` (boolean). Proxies are started or stopped dynamically.
Response: `{"status": "ports updated"}`.

### `POST /api/vms/:uuid/proxy`

Body: `{"vm_port": <int>, "host_port": <optional int>}`. Starts a Ghost Proxy forwarding traffic to `vm_port`.
- UDS Socket Path: `/tmp/cryo-<hex>_<vm_port>.sock`
- If `host_port` is specified, also binds TCP `0.0.0.0:<host_port>`.
Response: `{"status": "proxy_started", "uds": "<socket path>", "host_port": <port>}`.
>>>>>>> 816acd8 (feat(ui): enhance dashboard and login UI with improved accessibility and user experience)

## 🔗 Network links

Links configure point-to-point IP forwarding and iptables rules between two isolated VMs within the same project. Both VMs must belong to the caller (or caller is admin).

| Method | Endpoint | Body | Response |
| :--- | :--- | :--- | :--- |
| `GET` | `/api/links` | none | `[{"vm1_uuid": "vm-da05a06c", "vm2_uuid": "vm-1b2c3d4e"}]` |
| `POST` | `/api/links` | `{"vm1_id": "vm-da05a06c", "vm2_id": "vm-1b2c3d4e"}` | `{"status": "linked"}` |
| `DELETE` | `/api/links/:uuid1/:uuid2` | none | `{"status": "unlinked"}` |

<<<<<<< HEAD
In the `POST` body, `vm1_id` and `vm2_id` hold VM **UUIDs**. The call returns `400` with `{"error": "Cross-project linking is denied."}` whenever the manager rejects the pair. That is also what you get if one of the UUIDs does not exist. `500` is returned for other errors.

## 💾 Volumes

Volumes are extra ext4 disks stored as `volumes/vol_<id>.ext4`. A volume is attached to at most one VM, and is added to the VM as a Firecracker drive at boot. Volumes still use integer IDs.
=======
## 💾 Volumes

Persistent ext4 storage disks stored at `<BASE_PATH>/volumes/vol_<id>.ext4`. Volumes attach to at most one VM as an additional Firecracker drive.
>>>>>>> 816acd8 (feat(ui): enhance dashboard and login UI with improved accessibility and user experience)

| Method | Endpoint | Body | Response |
| :--- | :--- | :--- | :--- |
| `GET` | `/api/volumes` | none | `[{"id": 1, "name": "data", "size_gb": 2, "attached_vm_id": -1, "owner_id": 1}]` |
| `POST` | `/api/volumes` | `{"name": "data", "size_gb": 2}` | `{"status": "created", "id": 1}` |
| `DELETE` | `/api/volumes/:id` | none | `{"status": "deleted"}` |
<<<<<<< HEAD
| `POST` | `/api/volumes/:id/attach` | `{"vm_id": "vm-da05a06c"}` | `{"status": "attached"}` |
| `POST` | `/api/volumes/:id/detach` | none | `{"status": "detached"}` |

`attached_vm_id` in the listing is the VM's **slot** (an integer), or `-1` when the volume is not attached. The `vm_id` you send to `attach` is the VM's **UUID**.

=======
| `POST` | `/api/volumes/:id/attach` | `{"vm_id": "<uuid-or-id>"}` | `{"status": "attached"}` |
| `POST` | `/api/volumes/:id/detach` | none | `{"status": "detached"}` |

>>>>>>> 816acd8 (feat(ui): enhance dashboard and login UI with improved accessibility and user experience)
> [!NOTE]
> Attaching or detaching storage automatically reboots running VMs so the new drive is discovered. If the target VM is hibernated, its stale snapshot memory state is invalidated, transitioning it to `stopped` to ensure a clean cold boot on next launch.

