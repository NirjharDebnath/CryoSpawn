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
| `nginx` (optional) | `vmX.cryo` browser ingress |
| `nc` (optional) | SSH through a Unix socket |
| Uncompressed kernel (`vmlinux/ubuntu-vmlinux.bin`) | Guest kernel |
| Template root disk (`rootfs/ubuntu-rootfs.ext4`) | Copied for every VM |

The kernel and root disk are not part of the repository. You must provide them.

## ⚙️ Configure paths

> [!IMPORTANT]
> Several absolute paths are hard-coded to `/home/nirjhar/Python Codes/Einstein/CryoSpawn`. Change them to your checkout location before building, or the daemon cannot find its kernel, disks and UI.

| File | What is hard-coded |
| :--- | :--- |
| [internal/main.cpp](../internal/main.cpp) | `KERNEL_PATH`, `ROOTFS_PATH`, and the UI mount point (`set_mount_point`) |
| [internal/orchestrator.hpp](../internal/orchestrator.hpp) | `instances/` and `volumes/` directories, snapshot and memory file paths |

## 🔨 Build

From the `internal/` directory (also in [utils/cryo_server_compile.sh](../utils/cryo_server_compile.sh)):

```bash
g++ -std=c++17 main.cpp -o cryospawn -pthread -lsqlite3
```

## ▶️ Run

```bash
cd internal
sudo ./cryospawn
```

The daemon:

1. Detects the default network interface and installs the firewall baseline.
2. Opens (or creates) `cryospawn.db` in the current directory and re-adopts VMs recorded there.
3. Serves the dashboard on `http://localhost:8080` (the only port to expose, e.g. through a tunnel) and keeps the API on `127.0.0.1:9090`.

Run it from the same directory each time, because the database path is relative.

Open `http://localhost:8080` for the dashboard, or use the [HTTP API](API.md).

## ☁️ Share the dashboard with a Cloudflare tunnel

Only the dashboard port (`8080`) is meant to be exposed. The API on `127.0.0.1:9090` stays local, and the dashboard forwards `/api/*` to it.

1. Start the daemon (terminal 1):

   ```bash
   cd internal
   sudo ./cryospawn
   ```

2. Open a quick tunnel to the dashboard port (terminal 2):

   ```bash
   cloudflared tunnel --url http://localhost:8080
   ```

3. Share the `https://<random-words>.trycloudflare.com` URL that `cloudflared` prints.

> [!WARNING]
> Never tunnel port `9090`. Anyone with the tunnel URL can still control all VMs through `/api/*`, because there is no login. See [KNOWN-ISSUES.md](KNOWN-ISSUES.md).

## 🌐 Reach a VM from the host

### Browser (HTTP) through Nginx

1. Install the ingress config with [nginx/server.sh](../nginx/server.sh). It writes `/etc/nginx/conf.d/cryospawn.conf` and reloads Nginx.
2. Add a hosts entry for each VM slot:

   ```bash
   echo "127.0.0.1 vm0.cryo" | sudo tee -a /etc/hosts
   ```

3. Create the VM with `expose_http` enabled, start a web server inside it (see [utils/start_vm_server.sh](../utils/start_vm_server.sh)), and open `http://vm0.cryo`.

### SSH

Create the VM with `expose_ssh` enabled, then:

```bash
ssh -o ProxyCommand="nc -U /tmp/cryo_vm_0_22.sock" root@localhost
```

## 🧹 Cleanup helpers

| Script | Purpose |
| :--- | :--- |
| [utils/delete_tap.sh](../utils/delete_tap.sh) | Deletes every host network interface whose name starts with `cryo` (use after a crash) |
| [utils/loopback.sh](../utils/loopback.sh) | Example `/etc/hosts` entry for `vm0.cryo` |

## 🩺 Troubleshooting

| Symptom | Likely cause |
| :--- | :--- |
| VM stays in `booting` | Wrong kernel or root disk path, or `firecracker` is not on `PATH` |
| `Cannot open database` on start | The working directory is not writable |
| Dashboard returns 404 | The UI mount point path in `main.cpp` is wrong |
| `vmX.cryo` does not load | Missing hosts entry, Nginx not reloaded, or `expose_http` is off |
| Stale `cryoN` interfaces after a crash | Run `utils/delete_tap.sh` |

For how the pieces fit together, see [DESIGN.md](../DESIGN.md).
