# CryoSpawn Roadmap

This roadmap separates implemented behavior from future engineering work.

## Implemented

- C++17 Firecracker lifecycle engine with `booting`, `running`, `hibernated`, `waking`, `stopped`, `restarting`, and `terminating` states
- KVM TAP networking, per-VM `/30` subnets, NAT, isolation, and explicit VM links
- Unix-domain Ghost Proxies for exposed services and optional TCP host ports
- SQLite persistence, WAL mode, slot reuse, VM adoption, and legacy-row migration
- UUID-based VM names and host artifacts
- Rootfs sizing and attached ext4 volumes
- Flask login, registration, logout, password change, projects, and owner-scoped sessions
- C++ authentication checks for trusted identity headers and shared-secret support
- Dynamic dashboard views and API proxy
- Asset download script for supported kernel/rootfs images
- In-browser `xterm.js` Web Console (`frontend/ws_console.py`, `/console/<uuid>`, `/ws/console/<uuid>`)
- Named Cloudflare Tunnel + Nginx wildcard ingress (`nginx/server.sh`) with WebSocket `Upgrade` headers and `PrivateTmp=false`
- Modular God-Mode Host Admin Panel (`admin/` on `:9091`) with daemon lifecycle control, live logs, RootFS & Kernel Studio, `>_ Chroot` shell, SQLite Explorer, and Custom SQL Console
- Guest DevTools & Ubuntu 24.04 Minimal setup script (`utils/setup_guest_devtools.sh`)

## Phase 7: multi-user hardening - partial

Implemented: users, password hashes, sessions, owner IDs, project scoping, and API ownership checks.

Remaining:

- Stable deployment identity and production session configuration
- Audited authorization and group roles
- Resource and host quotas
- Production-safe daemon authentication and TLS boundary
- Comprehensive direct-daemon and browser security tests

## Phase 8: topology and branching - planned

- Save and restore complete multi-VM environments
- Interactive topology canvas
- Copy-on-write VM forks and snapshot branching
- Infrastructure-as-code import/export
- Environment-level lifecycle operations

## Phase 9: cryptographic mesh - planned

- Signed peering tokens and expiration
- Authenticated overlay transport
- Cross-host and WAN peering
- Per-link port policy
- Tenant-to-tenant isolation guarantees

## Phase 10: host administration - largely implemented

Implemented in `admin/` (`:9091`):
- Host CPU, memory, disk, TAP, and socket telemetry
- Daemon Start/Stop/Restart (`--kernel`, `--rootfs`, `stdbuf -oL -eL`) and live log viewer
- Emergency hibernate-all and stale TAP/socket cleanup
- Global `?all=true` administrative dashboard + `xterm.js` consoles
- Multi-RootFS & Kernel Studio (clone, Fix DNS, resize, `>_ Chroot`) + SQLite Explorer & Custom SQL Console

Remaining:
- Hard quotas for CPUs, memory, storage, and VM count
- Persistent security and lifecycle audit logs

## Documentation status

Use [docs/SETUP.md](docs/SETUP.md) for setup, [docs/API.md](docs/API.md) for endpoint behavior, [DESIGN.md](DESIGN.md) for architecture, and [docs/KNOWN-ISSUES.md](docs/KNOWN-ISSUES.md) for operational limits.
