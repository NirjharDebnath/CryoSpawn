# 🛠️ Setup Guide

> **Written for:** developers and operators who want to build and run the CryoSpawn daemon on a Linux machine.

## 📦 Prerequisites

CryoSpawn manages Firecracker MicroVMs, TAP devices and `iptables` rules, so it only runs on Linux and needs root.

| Requirement | Used for |
| :--- | :--- |
| Linux with KVM (`/dev/kvm`) | Firecracker virtualization |
| `firecracker` on `PATH` | Launched by the daemon for every VM |
| `g++` with C++17, `libsqlite3-dev` | Building the daemon |
| `iproute2` (`ip`), `iptables` | TAP devices, NAT and firewall rules |
| `e2fsprogs` (`e2fsck`, `resize2fs`, `mkfs.ext4`), `truncate` | Root disk expansion and volumes |
| Python 3 with `flask` and `requests` | The login and dashboard frontend ([frontend/app.py](../frontend/app.py)) |
| `nginx` (optional) | `cryo-<hex>.cryo` browser ingress |
| `nc` (optional) | SSH through a Unix socket |
| Uncompressed kernel (`vmlinux/ubuntu-vmlinux.bin`) | Guest kernel |
| Template root disk (`rootfs/ubuntu-rootfs.ext4`) | Copied for every VM |

The kernel and root disk are not part of the repository. You must provide them.

## 🗂️ Project layout on disk

The daemon locates its project root from the path of its own binary (`config::BASE_PATH` in [internal/config.hpp](../internal/config.hpp)): if the binary sits in `internal/`, the root is the folder above it, otherwise the folder containing the binary. No path needs editing.

| Path under the project root | What the daemon expects |
| :--- | :--- |
| `vmlinux/ubuntu-vmlinux.bin` | Guest kernel (you provide it) |
| `rootfs/ubuntu-rootfs.ext4` | Template root disk (you provide it) |
| `instances/`, `volumes/` | Created automatically |

> [!IMPORTANT]
> The SQLite file is opened as `cryospawn.db` **relative to the current directory**, while the Flask frontend opens `cryospawn.db` in the project root. Start the daemon from the project root so both processes share one database.

## 🔨 Build

From the `internal/` directory (also in [utils/cryo_server_compile.sh](../utils/cryo_server_compile.sh)):

```bash
g++ -std=c++17 main.cpp -o cryospawn -pthread -lsqlite3
```

Install the frontend dependencies once:

```bash
pip install flask requests
```

## ▶️ Run

CryoSpawn is two processes. Start them from the project root, each in its own terminal.

1. Daemon (needs root):

   ```bash
   sudo ./internal/cryospawn
   ```

2. Dashboard and login frontend:

   ```bash
   python frontend/app.py
   ```

The daemon:

1. Detects the default network interface and installs the firewall baseline.
2. Opens (or creates) `cryospawn.db` in the current directory and re-adopts VMs recorded there.
3. Serves the REST API on `127.0.0.1:9090` only.

The frontend creates the `users` table, serves the dashboard on `0.0.0.0:8080` and forwards `/api/*` to the daemon.

## 🔑 First login

Open `http://localhost:8080`. You are redirected to `/login`.

| Field | Default |
| :--- | :--- |
| Username | `admin` |
| Password | `admin123` |

The account is created only when the `users` table is empty. The "Update Password" button on the Settings page is not wired to anything yet, so to change the password, write a new hash into the database:

```bash
python - <<'PY'
import sqlite3
from werkzeug.security import generate_password_hash
c = sqlite3.connect("cryospawn.db")
c.execute("UPDATE users SET password_hash=? WHERE username='admin'", (generate_password_hash("a-new-password"),))
c.commit()
PY
```

> [!WARNING]
> The Flask `secret_key` in `frontend/app.py` is a placeholder. Replace it before exposing the dashboard to anyone else. See [KNOWN-ISSUES.md](KNOWN-ISSUES.md).

The dashboard has four pages: MicroVMs (`/dashboard`), Elastic Volumes (`/volumes`), Routing & Mesh (`/networks`) and Settings (`/settings`). You can also use the [HTTP API](API.md).

## ☁️ Share the dashboard with a Cloudflare tunnel

Only the Flask port (`8080`) is meant to be exposed. The API on `127.0.0.1:9090` stays local, and Flask checks the login session before forwarding `/api/*` to it.

1. Start the daemon and the frontend as described in [Run](#️-run).

2. Open a quick tunnel to the dashboard port:

   ```bash
   cloudflared tunnel --url http://localhost:8080
   ```

3. Share the `https://<random-words>.trycloudflare.com` URL that `cloudflared` prints.

> [!WARNING]
> Never tunnel port `9090`: it has no login. Change the default `admin` password first, because anyone with the tunnel URL can reach the login page. See [KNOWN-ISSUES.md](KNOWN-ISSUES.md).

## 🌐 Reach a VM from the host

### Browser (HTTP) through Nginx

Each VM has a UUID such as `vm-da05a06c`. Its HTTP socket is `/tmp/cryo-da05a06c_80.sock` (the UUID without the `vm-` prefix), and the host name is `cryo-da05a06c.cryo`.

1. Install an Nginx server block that maps `cryo-<hex>.cryo` to `/tmp/cryo-<hex>_80.sock`. The example config is in [DESIGN.md](../DESIGN.md#nginx-ingress-cryo-hexcryo--local-dns).
2. Add a hosts entry for each VM:

   ```bash
   echo "127.0.0.1 cryo-da05a06c.cryo" | sudo tee -a /etc/hosts
   ```

3. Create the VM with HTTP enabled, start a web server inside it (see [utils/start_vm_server.sh](../utils/start_vm_server.sh)), and open `http://cryo-da05a06c.cryo`.

> [!WARNING]
> [nginx/server.sh](../nginx/server.sh) still writes the older `vmN.cryo` pattern and `/tmp/cryo_vm_N_80.sock` socket names, which no longer match what the daemon creates. See [KNOWN-ISSUES.md](KNOWN-ISSUES.md).

For a public, wildcard-domain setup with Cloudflare, see [DNS.md](DNS.md).

### SSH

Create the VM with SSH enabled, then:

```bash
ssh -o ProxyCommand="nc -U /tmp/cryo-da05a06c_22.sock" root@localhost
```

The dashboard's copy button builds this command for you.

## 🧹 Cleanup helpers

| Script | Purpose |
| :--- | :--- |
| [utils/delete_tap.sh](../utils/delete_tap.sh) | Deletes every host network interface whose name starts with `cryo` (use after a crash) |
| [utils/loopback.sh](../utils/loopback.sh) | Example `/etc/hosts` entry (uses the older `vm0.cryo` naming) |

## 🩺 Troubleshooting

| Symptom | Likely cause |
| :--- | :--- |
| VM stays in `booting` | Missing kernel or root disk under the project root, or `firecracker` is not on `PATH` |
| `Cannot open database` on start | The working directory is not writable |
| Dashboard redirects to `/login` forever | Wrong credentials, or the `users` table lives in a different `cryospawn.db` than you expect |
| `502 C++ backend unavailable` | The daemon is not running on `127.0.0.1:9090` |
| Page or API call returns `401` | The session expired: log in again |
| `cryo-<hex>.cryo` does not load | Missing hosts entry, Nginx not pointing at `/tmp/cryo-<hex>_80.sock`, or HTTP is off for the VM |
| Stale `cryo-...` interfaces after a crash | Run `utils/delete_tap.sh` |

For how the pieces fit together, see [DESIGN.md](../DESIGN.md).
