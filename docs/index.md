# ❄️ CryoSpawn Documentation

> **CryoSpawn** is a lightweight, multi-tenant **Firecracker KVM MicroVM platform** with instant hibernation, **Ghost Proxy auto-wake**, an in-browser **`xterm.js` Web Console**, **Cloudflare Tunnel + Nginx** wildcard ingress, and a **God-Mode Host Admin Panel**.

[Open the Interactive Architecture Flow Map →](../){ .md-button .md-button--primary }

---

## How CryoSpawn Works (In Plain English)

Imagine running dozens of isolated Linux servers on a single laptop or homelab box without wasting gigabytes of idle RAM:

1. **C++17 Hypervisor Daemon (`127.0.0.1:9090`):** Talks directly to AWS Firecracker over Unix sockets, clones a Copy-on-Write `.ext4` root disk per VM, creates a private `/30` TAP network (`172.16.x.2`), and enforces zero-trust `iptables` isolation.
2. **Ghost Proxy & Instant Hibernation:** When a VM is idle, CryoSpawn snapshots its RAM + CPU state to disk and kills the Firecracker process (`0 MB` RAM, `0%` CPU). When a web request or SSH connection arrives on `/tmp/cryo-<hex>_<port>.sock`, Ghost Proxy wakes the VM in ~80 ms and delivers the traffic seamlessly.
3. **Tenant Web Dashboard (`0.0.0.0:8080`):** A Flask frontend (`frontend/app.py`) with session login, project workspaces, Elastic Volumes, Routing & Mesh, and an embedded **`xterm.js` Web Console**.
4. **Nginx + Cloudflare Tunnel Ingress:** Maps `cryospawn.<yourdomain>` to the dashboard (`:8080`) and `cryo-<hex>.<yourdomain>` to each VM's Unix socket (`_80.sock`) with zero router ports opened.
5. **God-Mode Host Admin Panel (`0.0.0.0:9091`):** A private host-only controller (`admin/app.py`) that starts/stops the C++ daemon, switches kernels (`vmlinux-6.18.51` / `ubuntu-vmlinux.bin`) and `.ext4` RootFS images, provides `>_ Chroot` shells, and includes an SQLite Explorer + Custom SQL Console.

---

## Documentation Map

| Guide | What You'll Learn |
| :--- | :--- |
| **[Setup & Quick Start](SETUP.md)** | Prerequisites, building the C++ daemon, downloading assets, and running all three services |
| **[Architecture & Design](DESIGN.md)** | Deep dive into `/30` subnets, `iptables` isolation, Ghost Proxy, hibernation, and SQLite schema |
| **[Web Console (`xterm.js`)](CONSOLE.md)** | How the zero-dependency RFC 6455 WebSocket + PTY SSH bridge works securely |
| **[God-Mode Admin & RootFS](ADMIN.md)** | Daemon lifecycle control, RootFS Studio, kernel-to-rootfs pairing, and Custom SQL Console |
| **[Cloudflare & Nginx Ingress](DNS.md)** | Exposing `cryospawn.<domain>` and `*.<domain>` safely without opening router ports |
| **[HTTP API Reference](API.md)** | Every REST & WebSocket endpoint across the daemon, tenant frontend, and admin panel |
| **[Known Issues & Status](KNOWN-ISSUES.md)** | Resolved engineering fixes and current platform limitations |
| **[Roadmap](ROADMAP.md)** | Completed milestones and upcoming features |
