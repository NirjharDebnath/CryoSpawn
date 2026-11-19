# 🐞 Known Issues & Architectural Status

> **Written for:** developers and operators who run or contribute to CryoSpawn and want to know its limitations and recent architectural fixes.

## 📋 Summary

<<<<<<< HEAD
| # | Issue | Impact | Where |
| :-: | :--- | :--- | :--- |
| 1 | Kernel and root disk images not in the repo | A fresh clone cannot boot a VM | repo-wide |
| 2 | New VMs are always owned by user 1 | Non-admin users do not see VMs they create | `main.cpp`, `orchestrator.hpp` |
| 3 | Daemon trusts `X-User-ID` and `X-Role` and skips checks on some routes | Anything that reaches port 9090 acts as any user | `main.cpp` |
| 4 | Default `admin` / `admin123` login and placeholder session key | Anyone who reaches the dashboard can log in as admin | `frontend/app.py` |
| 5 | No way to create users or change a password from the UI | Settings page and multi-user use need manual database edits | `frontend/app.py`, `settings.html` |
| 6 | Nginx ingress script uses the old `vmN.cryo` naming | Browser access to a VM fails with the generated config | `nginx/server.sh` |
| 7 | `host_port` is ignored by `/proxy`, which uses a slot-based socket name | The request field has no effect; socket naming is inconsistent | `main.cpp` |
| 8 | Root disk default differs between API and schema | Confusing `rootfs_gb` values | `main.cpp`, `database.hpp` |
=======
| # | Issue | Status | Resolution | Where |
| :-: | :--- | :---: | :--- | :--- |
| 1 | Hard-coded absolute paths | **RESOLVED** | Added `config.hpp` with env vars (`CRYOSPAWN_*`), `/proc/self/exe` binary discovery, and CLI flags (`--kernel`, `--rootfs`, `--port`). | `config.hpp`, `main.cpp`, `orchestrator.hpp` |
| 2 | `restart` creates a second VM | **RESOLVED** | Removed redundant `create_vm` call in `reboot_vm` flow; now cleanly restarts target slot only. | `main.cpp` |
| 3 | Kernel and root disk images not in repo | **RESOLVED** | Added `utils/download_assets.sh` fetching standard AWS Firecracker kernel & Ubuntu ext4 rootfs. | `utils/download_assets.sh` |
| 4 | API authentication & multi-tenant isolation | **RESOLVED** | Added role-based auth (`X-User-ID`, `X-Role`), password hashing (PBKDF2-HMAC-SHA256), session verification, user registration, and change-password endpoints. Protected static `.html` pages. | `main.cpp`, `app.py`, `database.hpp` |
| 5 | `host_port` ignored & ghost proxy bugs | **RESOLVED** | Added native TCP Ghost Proxy listener (`run_ghost_proxy_tcp`) alongside Unix Domain Sockets; fixed double-close and SIGPIPE concurrency bugs; added legacy `/tmp/cryo_vm_<slot>_<port>.sock` symlinks. | `proxy.hpp`, `main.cpp` |
| 6 | Root disk default mismatch | **RESOLVED** | Standardized `rootfs_gb` default across API, database schema, and UI to 3 GB (minimum for Ubuntu ext4). | `main.cpp`, `database.hpp`, `orchestrator.hpp` |
| 7 | Volumes on hibernated/stopped VMs | **RESOLVED** | Attaching/detaching storage on hibernated VMs invalidates snapshots, transitions state to `stopped`, and triggers clean cold boot via unified `start_or_resume_vm` on subsequent start/wake. | `main.cpp`, `orchestrator.hpp` |
| 8 | SQL injection & concurrency locks | **RESOLVED** | Parameterized all queries with prepared statements; enabled SQLite Write-Ahead Logging (WAL) and 5000ms busy timeout; implemented null-safe column text reading. | `database.hpp`, `app.py` |
| 9 | Slot allocation gap skipping | **RESOLVED** | Replaced flawed `min(slot_id) + 1` query with comprehensive slot gap finder that reuses slot 0 and any freed slots. | `database.hpp` |
| 10 | Reconcile state wiping stopped VMs & `kill(0,0)` bug | **RESOLVED** | Preserved stopped and hibernated VMs across daemon restarts; restricted process health check to positive PIDs (`kill(pid, 0)` for `pid > 0`). | `orchestrator.hpp` |
>>>>>>> 816acd8 (feat(ui): enhance dashboard and login UI with improved accessibility and user experience)

## 🔧 Details & Resolutions

<<<<<<< HEAD
### 1. Kernel and root disk images are not provided

`vmlinux/ubuntu-vmlinux.bin` and `rootfs/ubuntu-rootfs.ext4` are not in the repository, and no doc or script explains how to produce them. The daemon looks for them under the project root (see [SETUP.md](SETUP.md#️-project-layout-on-disk)).

- **Suggested fix:** add a script or a doc describing how to build or download compatible images.

### 2. New VMs are always owned by user 1

The daemon filters VM lists and checks ownership with `owner_id`. `POST /api/vms` does not pass the caller's ID to `create_vm()`, so the row is stored with the column default (`1`, the admin). In memory the field is never set either, until the daemon re-reads the database after a restart.

- **Effect:** a non-admin user can create a VM but will not see it in `GET /api/vms`. Admins see everything.
- **Suggested fix:** pass `user_id` into `create_vm()` and store it.

### 3. The daemon trusts identity headers

Flask adds `X-User-ID` and `X-Role` after checking the session, and the daemon believes them. Port 9090 listens on `127.0.0.1` only, but any local process can send those headers directly.

Some routes also never read them: `DELETE /api/vms/:uuid`, all `/api/links` routes and all `/api/volumes` routes. They do not require the headers and do not check ownership, so any logged-in user can delete any VM, link any pair, or attach any volume. The daemon runs as root and manages firewall rules.

> [!WARNING]
> Never expose port 9090. Expose only 8080, and only to people you trust.

### 4. Default credentials and session key

A first start creates `admin` with the password `admin123` when the `users` table is empty, and `frontend/app.py` signs sessions with a fixed placeholder `secret_key`. The frontend also runs Flask's built-in development server on `0.0.0.0:8080`.

- **Workaround:** change the admin password right away (see [SETUP.md](SETUP.md#-first-login)) and replace the secret key before sharing the dashboard, for example through a tunnel.

### 5. No user management in the UI

`frontend/app.py` has login, logout and "who am I" routes only. There is no register route, and the "Update Password" button on the Settings page has no handler behind it. Extra users have to be inserted into the `users` table by hand. Combined with issue 2, multi-user use is not working end to end yet.

### 6. Nginx ingress script is out of date

[nginx/server.sh](../nginx/server.sh) matches `vm<number>.cryo` and proxies to `/tmp/cryo_vm_<number>_80.sock`. The daemon now creates `/tmp/cryo-<hex>_80.sock` for the VM UUID `vm-<hex>`. The config in [DESIGN.md](../DESIGN.md#nginx-ingress-cryo-hexcryo--local-dns) is the one that matches. The same applies to the HTTPS block (`_443.sock`) and to the example in [utils/loopback.sh](../utils/loopback.sh).

### 7. `host_port` is ignored by `POST /api/vms/:uuid/proxy`

The body requires `host_port`, but only `vm_port` is used. The proxy listens on `/tmp/cryo_vm_<slot>_<vm_port>.sock`, a different naming scheme from the exposure sockets (`/tmp/cryo-<hex>_<port>.sock`). `host_port` is also a legacy column in the database.

### 8. Root disk size defaults differ

The API defaults `rootfs_gb` to `3`, while the database column defaults to `1`. The disk is only expanded when `rootfs_gb` is `3` or more, so values of `1` or `2` keep the template image size.

## ✅ Fixed since the first version of this list

| Former issue | Status |
| :--- | :--- |
| Hard-coded absolute paths | Paths now resolve from the binary's location ([internal/config.hpp](../internal/config.hpp)) |
| `restart` created a second VM | The handler now only reboots the requested VM |
| API had no authentication | Login and sessions were added in the Flask frontend; see issues 3 to 5 for what is still open |

## ❓ Needs verification

- **Volumes and hibernated VMs:** attaching or detaching a volume only reboots a VM whose status is `running`. Whether a hibernated VM picks up the change when it is woken from its snapshot has not been checked.
=======
### 1. Hard-coded absolute paths [RESOLVED]
Paths are dynamically determined relative to the executable binary location (`/proc/self/exe`) by default, and can be overridden via environment variables or command-line flags:
- `CRYOSPAWN_BASE_PATH`: Base root directory.
- `CRYOSPAWN_KERNEL_PATH`: Path to guest `vmlinux`.
- `CRYOSPAWN_ROOTFS_PATH`: Path to template rootfs image.
- `CRYOSPAWN_DB_PATH`: SQLite database path.
- `CRYOSPAWN_BACKEND_PORT`: HTTP API port (default 9090).
- CLI flags: `--kernel <path>`, `--rootfs <path>`, `--port <port>`.

### 2. `POST /api/vms/:uuid/restart` duplicate VM bug [RESOLVED]
The spurious secondary `create_vm()` call has been removed from the restart handler. Restarting an instance now solely stops the current Firecracker process and spawns a fresh process for that same slot. Added UI restart button and `restartVM()` in `app.js`.

### 3. Kernel and root disk images [RESOLVED]
The helper script `utils/download_assets.sh` automatically downloads the latest verified Firecracker kernel (`vmlinux-5.10`) and Ubuntu ext4 base rootfs into `vmlinux/` and `rootfs/`.

### 4. API authentication & security isolation [RESOLVED]
The backend enforces `X-User-ID` and `X-Role` headers on all routes. The Flask proxy manages authenticated sessions, signs session cookies, and forwards validated credentials to the C++ core daemon. Protected `.html` pages are guarded against direct unauthenticated requests. Multi-tenant isolation ensures non-admin users can only view, start, hibernate, proxy, attach volumes to, and link their own VMs.

### 5. `host_port` handling & ghost proxy concurrency [RESOLVED]
- Ghost Proxy now supports native TCP host binding (`run_ghost_proxy_tcp`) in addition to standard Unix Domain Sockets (`/tmp/cryo-<hex>_<port>.sock`).
- Relaying uses TCP half-close (`shutdown(..., SHUT_WR)`) and single-owner socket destruction, eliminating multithreaded double-close descriptor corruption.
- Relaying sends data with `MSG_NOSIGNAL` and ignores `SIGPIPE`, preventing client disconnects from crashing the daemon.
- Created legacy symlinks `/tmp/cryo_vm_<slot>_<port>.sock` so legacy routes (`http://vm0.cryo`) continue to resolve without configuration changes.

### 6. Root disk size defaults [RESOLVED]
All defaults across `database.hpp`, `orchestrator.hpp`, `main.cpp`, and the web dashboard UI are synchronized to `3 GB`.

### 7. Storage volumes on hibernated & stopped VMs [RESOLVED]
Firecracker snapshots serialize memory and device state. Modifying block device configurations on a hibernated VM invalidates snapshot memory references. CryoSpawn automatically purges snapshot files and transitions VM state to `stopped` in memory and DB. Unified `start_or_resume_vm` transparently cold-boots the VM with the new volume geometry on subsequent start or wake.

### 8. Database robustness & concurrency [RESOLVED]
SQLite Write-Ahead Logging (WAL mode) and busy timeouts prevent database lock contention between Flask and the C++ daemon. All SQL queries use prepared statements with parameter binding against SQL injection. Null-safe string conversion protects against null DB values.

### 9. Slot allocation [RESOLVED]
The slot allocation query finds the lowest available slot ID, including slot 0 and any gaps left by deleted VMs.
>>>>>>> 816acd8 (feat(ui): enhance dashboard and login UI with improved accessibility and user experience)
