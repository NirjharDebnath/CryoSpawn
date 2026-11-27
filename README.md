# ❄️ CryoSpawn

CryoSpawn is a multi-tenant **Firecracker KVM MicroVM platform** built with a C++17 hypervisor daemon, a Flask tenant dashboard with an embedded **`xterm.js` Web Console**, **Cloudflare Tunnel + Nginx** wildcard ingress, and a private **God-Mode Host Admin Panel**.

## ✨ Features

* **Instant Hibernation & Ghost Proxy Auto-Wake:** Idle VMs are snapshotted to disk (`0%` CPU, `0 MB` RAM). Incoming HTTP or SSH traffic to `/tmp/cryo-<hex>_<port>.sock` wakes the VM in ~80 ms and relays the connection seamlessly.
* **In-Browser `xterm.js` Web Console:** Click **`>_ Console`** on any VM with SSH enabled to get a full interactive Linux shell over WebSockets (`frontend/ws_console.py`)—works over LAN and Cloudflare Tunnels with zero open SSH ports.
* **Cloudflare Tunnel + Nginx Ingress:** Exposes the dashboard (`cryospawn.<domain>`) and every VM (`cryo-<hex>.<domain>`) over HTTPS with zero router port forwarding.
* **Zero-Trust `/30` Networking:** Each VM gets an isolated `/30` TAP subnet (`172.16.x.2`), outbound internet NAT, LAN/host `REJECT` rules, and opt-in same- or cross-project VM peering.
* **God-Mode Host Admin Panel (`:9091`):** Separate private controller (`admin/`) to start/stop the C++ daemon, view live logs, switch kernels (`vmlinux-6.18.51` / `ubuntu-vmlinux.bin`), clone & customize `.ext4` RootFS images via `>_ Chroot`, and query SQLite.
* **Elastic Volumes & Dynamic Disks:** Configurable root disk sizes (`>= 3 GB`) and attachable ext4 volumes (`volumes/vol_<id>.ext4`).

## 🧭 Architecture Overview

```mermaid
flowchart LR
    Internet[Browser / Clients] -->|HTTPS| CF[Cloudflare Edge + Tunnel]
    CF -->|http://127.0.0.1:80| N[Nginx Reverse Proxy]
    N -->|cryospawn.domain| FL[Flask Dashboard :8080]
    N -->|cryo-hex.domain| S80[/tmp/cryo-hex_80.sock]
    FL -->|ws/console| WS[PTY SSH Bridge]
    WS --> S22[/tmp/cryo-hex_22.sock]
    FL -->|"127.0.0.1:9090 + X-User-ID"| D[CryoSpawn C++ Daemon]
    ADM[God-Mode Admin :9091] -->|controls & ?all=true| D
    S80 --> G[Ghost Proxy]
    S22 --> G
    G -->|wakes if hibernated| D
    D --> F[Firecracker MicroVMs]
    D --> DB[(SQLite cryospawn.db)]
    FL --> DB
    ADM --> DB
```

## 🚀 Quick Start

```bash
# 1. Build the C++ daemon
cd internal
g++ -std=c++17 main.cpp -o cryospawn -pthread -lsqlite3
cd ..

# 2. Download kernel & base rootfs (if not already present)
bash utils/download_assets.sh

# 3. Install Python dependencies (in your virtualenv)
pip install flask requests
```

Start the **God-Mode Admin Panel** (starts/stops the C++ daemon as `root`) and the **Tenant Frontend**:

```bash
# Terminal 1: God-Mode Admin Panel (0.0.0.0:9091)
sudo CRYOSPAWN_ADMIN_PASSWORD="your-admin-password" python admin/app.py

# Terminal 2: Tenant Dashboard (0.0.0.0:8080)
export CRYOSPAWN_PUBLIC_DOMAIN="zibai.space"   # optional
python frontend/app.py
```

Or run the C++ daemon directly without the admin panel:

```bash
sudo ./internal/cryospawn
python frontend/app.py
```

Open `http://localhost:8080` (default first-start user: `admin` / `admin123`—change or remove it right away) and `http://localhost:9091` for the God-Mode Admin Panel.

## 📚 Documentation

* **[Documentation Site (Zensical)](docs/index.md)** · **[Interactive Flow Map](site/index.html)**
* [Setup & Quick Start](docs/SETUP.md)
* [Architecture & Design](DESIGN.md)
* [Web Console (`xterm.js`)](docs/CONSOLE.md)
* [God-Mode Admin & RootFS Studio](docs/ADMIN.md)
* [Cloudflare & Nginx Ingress](docs/DNS.md)
* [HTTP API Reference](docs/API.md)
* [Known Issues & Status](docs/KNOWN-ISSUES.md)
* [Engineering Roadmap](ROADMAP.md)
