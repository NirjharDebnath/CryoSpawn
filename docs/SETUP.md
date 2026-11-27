# 🛠️ Setup & Quick Start Guide

## 📦 Prerequisites (Linux with KVM)

| Requirement | Used for |
| :--- | :--- |
| Linux with `/dev/kvm` | Firecracker hardware virtualization |
| `firecracker` on `PATH` | MicroVM hypervisor |
| `g++` (C++17), `libsqlite3-dev` | Building the C++ daemon |
| `iproute2` (`ip`), `iptables` | TAP interfaces, NAT, and zero-trust firewall rules |
| `e2fsprogs` (`e2fsck`, `resize2fs`, `mkfs.ext4`), `truncate` | RootFS expansion & Elastic Volumes |
| Python 3 with `flask`, `requests`, `werkzeug` | Tenant frontend (`:8080`) and God-Mode Admin (`:9091`) |
| `ssh`, `nc` (`netcat-openbsd`) | In-browser `xterm.js` Web Console |
| `nginx`, `cloudflared` (optional) | Public wildcard HTTPS ingress (`*.yourdomain.com`) |

---

## 🔨 Build & Download Assets

```bash
# 1. Build the C++ daemon
cd internal
g++ -std=c++17 main.cpp -o cryospawn -pthread -lsqlite3
cd ..

# 2. Download kernel & base rootfs (if not already in vmlinux/ and rootfs/)
bash utils/download_assets.sh

# 3. Install Python dependencies in your virtualenv
pip install flask requests
```

---

## ▶️ Running CryoSpawn

### Option A: With the God-Mode Admin Panel (Recommended)

1. **Terminal 1 — God-Mode Admin Panel (`0.0.0.0:9091`, runs as `root`):**
   ```bash
   sudo CRYOSPAWN_ADMIN_PASSWORD="your-strong-password" ../einsteinenv/bin/python admin/app.py
   ```
   Open `http://<host-lan-ip>:9091` (or `http://127.0.0.1:9091`), sign in as `hostadmin`, pick your Kernel & RootFS in **RootFS Studio**, and click **Start** on the daemon.

2. **Terminal 2 — Tenant Dashboard (`0.0.0.0:8080`):**
   ```bash
   export CRYOSPAWN_SECRET_KEY="$(python3 -c 'import secrets; print(secrets.token_hex(32))')"
   export CRYOSPAWN_PUBLIC_DOMAIN="zibai.space"   # optional
   python frontend/app.py
   ```

### Option B: Standalone Daemon + Frontend

```bash
# Terminal 1: C++ daemon (root)
sudo ./internal/cryospawn

# Terminal 2: Tenant dashboard
python frontend/app.py
```

---

## 🔑 First Login & Preparing a Guest RootFS

* Open `http://localhost:8080` (first-start default: `admin` / `admin123`—change or remove it via **Settings** or the Admin Panel's **Users & SQLite** page).
* For **Ubuntu 24.04 Minimal** (`ubuntu-24.04-minimal.ext4`), pair it with **`vmlinux-6.18.51`** in **RootFS Studio**, resize to `3 GB` if installing dev tools, and run [`utils/setup_guest_devtools.sh`](../utils/setup_guest_devtools.sh) inside **`>_ Chroot`** to configure DNS (`1.1.1.1`), `ssh.service`, GCC/G++, Python 3, `nc`, `setsid`, `nano`, and `neofetch`.

---

## 🧪 Running Tests

```bash
bash tests/run_tests.sh
PYTHONPATH=".venv/lib/python3.12/site-packages:." python3.12 -m unittest tests/test_admin_panel.py tests/test_frontend_auth.py
```
