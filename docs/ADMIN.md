# ⚡ God-Mode Host Admin Panel & RootFS Studio

The **God-Mode Host Admin Panel** (`admin/app.py`, port `9091`) is a private host-controller web app designed for the bare-metal / laptop owner. It runs separately from the tenant-facing `frontend/app.py` (`:8080`) so administrative powers are never exposed to the public internet.

> [!CAUTION]
> Bind port `9091` only on your private LAN (`0.0.0.0:9091`) or `127.0.0.1:9091`. **Never** expose port `9091` in Nginx or Cloudflare Tunnels.

---

## Modular Architecture (`admin/`)

| Module | Responsibility |
| :--- | :--- |
| `admin/app.py` | Flask entrypoint, separate `cryospawn_admin_session` cookie, admin login, and multi-page routes (`/`, `/vms`, `/rootfs`, `/database`) |
| `admin/config.py` | Path resolution, active RootFS (`.active_rootfs`) & Kernel (`.active_kernel`) persistence, `admin_required` guard |
| `admin/daemon_routes.py` | Start/Stop/Restart C++ daemon (`--kernel` + `--rootfs` + `stdbuf -oL -eL`), live logs, host telemetry, emergency hibernate/cleanup, `?all=true` API proxy, VM `xterm.js` WebSocket |
| `admin/rootfs_routes.py` | Multi-RootFS listing, cloning, resizing, deleting, `Fix DNS`, Kernel selection, and `/ws/admin/rootfs-shell` `xterm.js` chroot |
| `admin/sqlite_routes.py` | Tabbed SQLite Database Explorer, Custom SQL Console (`/api/admin/sqlite/query`), and user account management |

---

## Multi-Page UI (`admin/ui/`)

1. **Overview & Daemon (`/`):**
   * **Start / Stop / Restart** `./internal/cryospawn` directly from the browser (when `admin/app.py` is started with `sudo`, the C++ daemon inherits `root` automatically).
   * **Live Daemon Logs:** Launches the C++ daemon under `stdbuf -oL -eL` so `std::cout` flushes line-by-line in real time to `logs/cryospawn.log`.
   * **Host Telemetry:** CPU load average, RAM used/total, disk free, active `cryo-*` TAP interfaces, and `/tmp/cryo-*.sock` sockets.
   * **Emergency Actions:** **Emergency Hibernate All** and **Cleanup Orphaned TAPs/Sockets** (deletes stale TAPs cleanly via `ip link set dev <tap> down` + `ip link delete <tap> type tun`).

2. **Global MicroVMs (`/vms`):**
   * Lists **all** VMs across **all** users (`?all=true`), allows deploying VMs for any `owner_id`, toggling ports, hibernating/waking, and opening **`>_ Console`** into any VM.

3. **RootFS Studio (`/rootfs`):**
   * **Kernel Selector:** Switch between kernels in `vmlinux/` (e.g. `vmlinux-6.18.51` vs `ubuntu-vmlinux.bin`).
   * **Multi-RootFS Manager:** List all `.ext4` images in `rootfs/`, **Clone** base images into specialized variants (e.g. `ubuntu-24.04-minimal.ext4` → `ubuntu-java.ext4`, `ubuntu-python-cpp.ext4`), **Fix DNS** (`1.1.1.1` / `8.8.8.8`), **Resize** (via `truncate` + `e2fsck` + `resize2fs`), open **`>_ Chroot`**, and click **Use & Restart Daemon**.

4. **Users & SQLite (`/database`):**
   * Create/delete user accounts (while keeping `0666` permissions on `cryospawn.db` for the non-root `frontend/app.py`).
   * Browse every SQLite table in structured HTML tables and run ad-hoc queries or scripts in the **Custom SQL Console**.

---

## Important Kernel ↔ RootFS Pairing

| RootFS Image | Guest OS / `systemd` | Required Kernel in `vmlinux/` |
| :--- | :--- | :--- |
| `ubuntu-24.04-minimal*.ext4` | Ubuntu 24.04 (`systemd` 255) | **`vmlinux-6.18.51`** (Linux 6.18.51+) |
| `ubuntu-rootfs.ext4` | Legacy Ubuntu 18.04 | **`ubuntu-vmlinux.bin`** (Linux 4.14.174) |

> [!TIP]
> Ubuntu 24.04 defaults to socket-activated `ssh.socket`, which can hang on Firecracker kernels. [`utils/setup_guest_devtools.sh`](../utils/setup_guest_devtools.sh) automatically switches it to direct `ssh.service` (`systemctl disable ssh.socket && systemctl enable ssh.service`), fixes `/tmp` `1777` permissions, creates `/var/cache/apt/archives/partial` and `/var/log/apt`, and installs GCC/G++ 13.3, Python 3.12, `cmake`, `git`, `nc`, `setsid`, `nano`, and `neofetch`.
