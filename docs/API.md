# CryoSpawn HTTP API

CryoSpawn exposes two HTTP layers:

| Layer | Address | Purpose |
| :--- | :--- | :--- |
| Flask frontend | `0.0.0.0:8080` | Login, sessions, projects, dashboard pages, and `/api/*` proxy |
| C++ daemon | `127.0.0.1:9090` | VM, link, routing, and volume operations |

Use the Flask port for normal clients. The daemon port is loopback-only and requires trusted identity headers.

## Conventions

- JSON request and response bodies use `application/json`.
- A successful response is normally `200` or `201`; errors use a non-2xx status and `{"error":"..."}`.
- VM identifiers are UUID strings such as `vm-da05a06c`. Numeric slot IDs are internal.
- Non-admin sessions can act only on their own VMs and volumes.
- The daemon accepts `X-User-ID` and `X-Role: admin|user`.
- Set `CRYOSPAWN_INTERNAL_SECRET` on both processes to require `X-Internal-Secret` on daemon requests.

## Authentication

| Method | Endpoint | Description |
| :--- | :--- | :--- |
| `POST` | `/api/auth/login` | Login with `username` and `password`; creates a session cookie |
| `POST` | `/api/auth/register` | Create a normal user (password must be at least 6 characters) |
| `POST` | `/api/auth/change-password` | Change the current user's password |
| `POST` | `/api/auth/logout` | Clear the session |
| `GET` | `/api/auth/me` | Return the current user |

```bash
curl -c jar -X POST http://localhost:8080/api/auth/login \
  -H 'Content-Type: application/json' \
  -d '{"username":"admin","password":"admin123"}'
```

The first start creates `admin` / `admin123` if the `users` table is empty. Change this password immediately.

## Projects

| Method | Endpoint | Body | Description |
| :--- | :--- | :--- | :--- |
| `GET` | `/api/projects` | none | List projects for the session user |
| `POST` | `/api/projects` | `{"name":"lab","description":"optional"}` | Create a project |
| `GET` | `/api/projects/:name` | none | Return project details and VM counts |
| `DELETE` | `/api/projects/:name` | none | Delete an empty project |

Project names use 1-64 characters: letters, numbers, spaces, `.`, `_`, and `-`.

## MicroVMs

| Method | Endpoint | Description |
| :--- | :--- | :--- |
| `GET` | `/api/vms` | List accessible VMs |
| `GET` | `/api/vms/:uuid` | Get one VM |
| `POST` | `/api/vms` | Create and boot a VM |
| `DELETE` | `/api/vms/:uuid` | Terminate and remove a VM |
| `POST` | `/api/vms/:uuid/hibernate` | Snapshot and stop a VM |
| `POST` | `/api/vms/:uuid/wake` | Resume a snapshot or cold boot |
| `POST` | `/api/vms/:uuid/start` | Alias for wake/start |
| `POST` | `/api/vms/:uuid/restart` | Reboot an existing VM |
| `PATCH` | `/api/vms/:uuid/ports` | Update SSH, HTTP, HTTPS, and routing exposure |
| `PATCH` or `POST` | `/api/vms/:uuid/routing` | Update inter-project routing |
| `POST` | `/api/vms/:uuid/proxy` | Create a custom Ghost Proxy |

### VM fields

`GET /api/vms` returns fields including `id`, `uuid`, `name`, `ip`, `host_ip`, `status`, `vcpus`, `mem_mib`, `rootfs_gb`, `tap_name`, `ssh_exposed`, `http_exposed`, `https_exposed`, `project`, `owner_id`, and `allow_interproject_routing`.

Lifecycle statuses include `booting`, `running`, `hibernated`, `waking`, `stopped`, `restarting`, and `terminating`.

### Create a VM

```bash
curl -b jar -X POST http://localhost:8080/api/vms \
  -H 'Content-Type: application/json' \
  -d '{"name":"web-1","vcpus":2,"mem_mib":1024,"expose_http":true,"project":"lab","rootfs_gb":5}'
```

| Field | Type | Default | Limits |
| :--- | :--- | :--- | :--- |
| `name` | string | VM name uses a slot-derived default | 1-64 valid characters |
| `vcpus` | integer | `1` | `1-32` |
| `mem_mib` | integer | `512` | `128-65536` |
| `expose_ssh` | boolean | `false` | Port 22 |
| `expose_http` | boolean | `false` | Port 80 |
| `expose_https` | boolean | `false` | Port 443 |
| `project` | string | `"default"` | Valid project name |
| `rootfs_gb` | integer | `3` | Minimum `3`, in GB |
| `allow_interproject_routing` | boolean | `false` | Opts the VM into cross-project links |

The create response is immediate with status `booting`. Poll `GET /api/vms/:uuid` until status is `running`.

Admin-only VM creation can set `owner_id` when the request includes `?all=true`.

### Restart, hibernate, and wake

```bash
curl -b jar -X POST http://localhost:8080/api/vms/vm-da05a06c/hibernate
curl -b jar -X POST http://localhost:8080/api/vms/vm-da05a06c/wake
curl -b jar -X POST http://localhost:8080/api/vms/vm-da05a06c/restart
```

Hibernation snapshots memory and CPU state. Wake or start resumes the snapshot when available, otherwise it performs a clean boot.

### Update ports and routing

```bash
curl -b jar -X PATCH http://localhost:8080/api/vms/vm-da05a06c/ports \
  -H 'Content-Type: application/json' \
  -d '{"ssh_exposed":true,"http_exposed":true,"https_exposed":false}'
```

Port proxies use `/tmp/cryo-<hex>_<port>.sock`. Missing port fields retain their current values.

### Custom proxy

```bash
curl -b jar -X POST http://localhost:8080/api/vms/vm-da05a06c/proxy \
  -H 'Content-Type: application/json' \
  -d '{"vm_port":3000,"host_port":13000}'
```

This creates `/tmp/cryo-da05a06c_3000.sock` and, when requested, binds `0.0.0.0:13000`.

## Links

Links require both VMs to belong to the caller, unless an admin uses `?all=true`. Cross-project links require routing enabled on both VMs.

| Method | Endpoint | Body |
| :--- | :--- | :--- |
| `GET` | `/api/links` | none |
| `POST` | `/api/links` | `{"vm1_id":"vm-da05a06c","vm2_id":"vm-1b2c3d4e"}` |
| `DELETE` | `/api/links/:uuid1/:uuid2` | none |
| `DELETE` | `/api/links` | `{"vm1_id":"...","vm2_id":"..."}` |

## Volumes

| Method | Endpoint | Body |
| :--- | :--- | :--- |
| `GET` | `/api/volumes` | none |
| `GET` | `/api/volumes/:id` | none |
| `POST` | `/api/volumes` | `{"name":"data","size_gb":10}` |
| `DELETE` | `/api/volumes/:id` | none |
| `POST` | `/api/volumes/:id/attach` | `{"vm_id":"vm-da05a06c"}` |
| `POST` | `/api/volumes/:id/detach` | none |

Volume names use the same 1-64 character resource-name rules as VM names. Sizes range from 1-1000 GB. A volume can attach to at most one VM. Attaching or detaching reboots a running VM and invalidates a hibernated VM's snapshot so it cold-boots with the new drives.
