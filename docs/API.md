# 🔌 CryoSpawn HTTP API

> **Written for:** developers and operators who call the CryoSpawn daemon directly (scripts, `curl`, the dashboard frontend).

The daemon serves a JSON REST API on `http://127.0.0.1:9090` (local only). A second listener on `0.0.0.0:8080` serves the web dashboard from `internal/ui/` and forwards `/api/*` to the API, so `http://localhost:8080/api/...` also works. Routes are defined in [internal/main.cpp](../internal/main.cpp).

## 📋 Conventions

- Request and response bodies are JSON (`Content-Type: application/json`).
- Errors return a non-2xx status and a body like `{"error": "..."}`.
- CORS is open: every response carries `Access-Control-Allow-Origin: *`.
- `:id` is the VM **slot ID** (0, 1, 2, ...), see [DESIGN.md](../DESIGN.md#3-database-design-the-systems-memory).
- There is no authentication. Expose only port 8080 (dashboard), never 9090. Anyone who can reach 8080 can still use the API through it.

## 🖥️ MicroVMs

| Method | Endpoint | Purpose |
| :--- | :--- | :--- |
| `GET` | `/api/vms` | List all VMs |
| `POST` | `/api/vms` | Create and boot a VM |
| `DELETE` | `/api/vms/:id` | Terminate a VM and remove its resources |
| `POST` | `/api/vms/:id/hibernate` | Snapshot to disk and stop the Firecracker process |
| `POST` | `/api/vms/:id/wake` | Restore a hibernated VM |
| `POST` | `/api/vms/:id/restart` | Reboot request (see the warning below) |
| `PATCH` | `/api/vms/:id/ports` | Turn SSH, HTTP and HTTPS exposure on or off |
| `POST` | `/api/vms/:id/proxy` | Start a Ghost Proxy for an arbitrary VM port |

### `GET /api/vms`

Returns an array of VM objects.

| Field | Type | Meaning |
| :--- | :--- | :--- |
| `id` | int | Slot ID |
| `ip` | string | Guest IP, e.g. `172.16.0.2` |
| `host_ip` | string | Host-side TAP gateway IP |
| `status` | string | `booting`, `running`, `hibernated` or `waking` |
| `vcpus` | int | Virtual CPU count |
| `mem_mib` | int | RAM in MiB |
| `tap_name` | string | Host TAP device, e.g. `cryo0` |
| `ssh_exposed`, `http_exposed`, `https_exposed` | bool | Ghost Proxy exposure flags |
| `host_port` | int | Legacy field |
| `host_lan_ip` | string | LAN IP of the host's active interface |
| `project` | string | Project namespace |

### `POST /api/vms`

All fields are optional. The body itself may be omitted.

| Field | Type | Default | Notes |
| :--- | :--- | :--- | :--- |
| `vcpus` | int | `1` | |
| `mem_mib` | int | `512` | |
| `expose_ssh` | bool | `false` | Starts the port 22 Ghost Proxy |
| `expose_http` | bool | `false` | Starts the port 80 Ghost Proxy |
| `expose_https` | bool | `false` | Starts the port 443 Ghost Proxy |
| `project` | string | `"default"` | VMs can only be linked within one project |
| `rootfs_gb` | int | `3` | Root disk size. Values of `3` or more expand the disk; smaller values keep the template size |

```bash
curl -X POST http://localhost:8080/api/vms \
  -H 'Content-Type: application/json' \
  -d '{"vcpus":2,"mem_mib":1024,"expose_http":true,"project":"lab1","rootfs_gb":5}'
```

Response: `{"id": 0, "ip": "172.16.0.2", "status": "booting"}`. Booting continues in the background, so poll `GET /api/vms` until `status` is `running`.

Errors: `400` for invalid JSON, `500` if creation fails.

### `DELETE /api/vms/:id`

Response: `{"status": "deleted"}`.

### `POST /api/vms/:id/hibernate` and `/wake`

Responses: `{"status": "hibernated"}` and `{"status": "running"}`. Both return `500` on failure. Waking also happens automatically when traffic reaches a Ghost Proxy.

### `POST /api/vms/:id/restart`

Response: `{"status": "restarting"}`.

> [!WARNING]
> The handler reboots slot `:id`, then calls `create_vm(1, 512, false, false, false, "default", 1)`, which allocates a **new** slot with default settings instead of re-creating the original one. Check the behavior before relying on it.

### `PATCH /api/vms/:id/ports`

Body fields `ssh_exposed`, `http_exposed`, `https_exposed` (bool, each defaults to `false`). Proxies are started or stopped to match. Response: `{"status": "ports updated"}`; `404` if the VM is unknown.

### `POST /api/vms/:id/proxy`

Body: `{"host_port": <int>, "vm_port": <int>}`. Only `vm_port` is used: it starts a Ghost Proxy on `/tmp/cryo_vm_<id>_<vm_port>.sock` forwarding to that port on the VM. Response: `{"status": "proxy_started", "uds": "<socket path>"}`. Errors: `404` unknown VM, `400` invalid request.

## 🔗 Network links

Links open traffic between two otherwise isolated VMs. See [DESIGN.md](../DESIGN.md#zero-trust-subnet-isolation--inter-vm-linking).

| Method | Endpoint | Body | Response |
| :--- | :--- | :--- | :--- |
| `GET` | `/api/links` | none | `[{"vm1": 0, "vm2": 1}]` |
| `POST` | `/api/links` | `{"vm1": 0, "vm2": 1}` | `{"status": "linked"}` |
| `DELETE` | `/api/links/:vm1/:vm2` | none | `{"status": "unlinked"}` |

`POST` returns `400` with `{"error": "Cross-project linking is denied."}` when the two VMs are rejected by the manager, and `500` for other errors.

## 💾 Volumes

Volumes are extra ext4 disks stored as `volumes/vol_<id>.ext4`. A volume is attached to at most one VM, and is added to the VM as a Firecracker drive at boot.

| Method | Endpoint | Body | Response |
| :--- | :--- | :--- | :--- |
| `GET` | `/api/volumes` | none | `[{"id": 1, "name": "data", "size_gb": 2, "attached_vm_id": -1}]` |
| `POST` | `/api/volumes` | `{"name": "data", "size_gb": 2}` | `{"status": "created", "id": 1}` |
| `DELETE` | `/api/volumes/:id` | none | `{"status": "deleted"}` |
| `POST` | `/api/volumes/:id/attach` | `{"vm_id": 0}` | `{"status": "attached"}` |
| `POST` | `/api/volumes/:id/detach` | none | `{"status": "detached"}` |

`attached_vm_id` is `-1` when the volume is not attached.

> [!NOTE]
> Attaching or detaching reboots the target VM if it is `running`, so the change takes effect. Expect a short interruption.
