# 🐞 Known Issues

> **Written for:** developers and operators who run or contribute to CryoSpawn and want to know its current limitations.

These were found by reading the source. None of them has been reproduced by running the daemon.

## 📋 Summary

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

## 🔧 Details

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
