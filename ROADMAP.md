# CryoSpawn: Platform Vision & Engineering Roadmap

## 1. Executive Vision & End Goal

**CryoSpawn** is an ultra-fast, zero-trust, collaborative MicroVM cloud and virtual lab platform running locally on top of Linux KVM and AWS Firecracker. 

The end goal is to deliver an enterprise-grade, self-hosted, lightweight alternative to heavy hypervisors and cloud sandbox providers (like Fly.io, Docker Desktop, or Proxmox) with:
- **Instant Hibernation & Auto-Wake (Ghost Proxy):** MicroVMs freeze to disk and resume in sub-100ms when traffic hits them.
- **Zero Host Pollution:** Host TCP ports remain untouched using dynamic Unix Domain Sockets (UDS) and Nginx wildcard ingress (`*.cryo`).
- **Dynamic Sparse Storage:** User-selectable disk sizes with copy-on-write storage isolation.
- **Zero-Trust VM Mesh Peering:** Cryptographically authenticated handshakes for inter-user and inter-project VM networking.
- **Topology Lab Hub:** One-click snapshotting, cloning, and branching of multi-VM virtual networks and cybersecurity/developer labs.
- **Multi-Tenant RBAC & Host "God-Mode":** Fine-grained permission boundaries for users and projects alongside an omnipotent host administrative dashboard.

---

## 2. Architecture & State Overview

```
                      +---------------------------------------+
                      |         HOST BROWSER / CLIENTS        |
                      +-------------------+-------------------+
                                          |
                        HTTP / HTTPS (http://vmX.cryo)
                                          v
                      +---------------------------------------+
                      |       Host Nginx Reverse Proxy        |
                      +-------------------+-------------------+
                                          | proxy_pass http://unix:/tmp/cryo_vm_X_80.sock
                                          v
      +-----------------------------------------------------------------------+
      | CryoSpawn Daemon & Ghost Proxy Layer (C++17 Orchestrator)             |
      |   - Listens on `/tmp/cryo_vm_<id>_<port>.sock`                        |
      |   - If VM is Hibernated: Intercepts syn, forks/resumes Firecracker    |
      |   - Pipes bidirectional stream into VM IP:Port                        |
      +-----------------------------------+-----------------------------------+
                                          |
                   TAP Interfaces (cryo0, cryo1, cryoX)
                                          v
      +-----------------------------------------------------------------------+
      | Firecracker KVM MicroVMs                                              |
      |   - Isolated copy-on-write ext4 rootfs                                |
      |   - Dedicated subnets (172.16.x.2/30) & default gateway (172.16.x.1)  |
      |   - Zero-Trust Cryptographic Overlay Mesh for inter-VM peering        |
      +-----------------------------------------------------------------------+
```

---

## 3. Completed Milestones (Current State)

- [x] **Phase 1: MicroVM Lifecycle Engine**
  - Forking Firecracker processes over custom Unix API sockets.
  - Subprocess PID tracking, process reaping, and SQLite persistent state.
- [x] **Phase 2: Snapshotting & Instant Hibernation**
  - Pausing VM CPU execution, dumping full memory (`.ram`) and CPU state (`.snap`).
  - Restoring running instances from snapshot with sub-100ms resume latency.
  - Isolated instance rootfs disks to prevent corrupting the golden image.
- [x] **Phase 3: Network Virtualization & TAP Management**
  - Dedicated `/30` point-to-point subnets per VM slot.
  - Idempotent host baseline firewall rules and outbound internet masquerading.
  - Software bridging and direct TAP-to-TAP forwarding for VM isolation.
- [x] **Phase 4: Zero Host Port Ghost Proxy & Nginx Ingress**
  - Complete elimination of host TCP port reservations (8000, 8400, 2200).
  - Proxy migrated to Unix Domain Sockets (`/tmp/cryo_vm_<id>_<port>.sock`).
  - Wildcard dynamic Nginx config (`~^vm(?<vmid>\d+)\.cryo$`).
  - SSH over Unix Socket via OpenSSH `ProxyCommand` (`nc -U`).
  - Concurrency locks to eliminate race conditions on simultaneous HTTP wakeups.
  - Resilient daemon state reconciliation: adopted VMs correctly preserve hibernation status and auto-respawn Ghost Proxies.
- [x] **Phase 5: Guest Test Suite**
  - Scripted guest web server setup (`utils/start_vm_server.sh`) for testing HTTP (80) and HTTPS (443) auto-wake workflows.

---

## 4. Engineering Roadmap: Next Phases

```
   [Phase 6: Dynamic Disk Sizing]
                 |
                 v
   [Phase 7: Multi-User Auth & RBAC]
                 |
                 v
   [Phase 8: Topology Hub & Branching]
                 |
                 v
   [Phase 9: Cryptographic Zero-Trust VM Mesh]
                 |
                 v
   [Phase 10: Host Admin "God-Mode" Panel]
```

### Phase 6: Dynamic User-Selectable Disk Sizing (Rootfs Resizing)
* **Goal:** Allow users to choose custom virtual disk capacity (e.g., 2 GB, 10 GB, 50 GB) at creation time instead of a static disk size.
* **Key Tasks:**
  1. Add a disk size selector to the UI spawn modal (`Disk Capacity (GB)`).
  2. Implement fast sparse resizing in `orchestrator.hpp`:
     - Clone golden base image to instance file.
     - Expand sparse container size (`truncate -s <size>G vm_<id>_rootfs.ext4`).
     - Expand filesystem structure safely without data loss (`e2fsck -fy` + `resize2fs`).
  3. Store disk quota in SQLite database (`disk_size_gib`).
  4. Display active storage allocation and disk consumption in the UI.

---

### Phase 7: Multi-Tenant Authentication & Project Scoping (RBAC)
* **Goal:** Enable multiple users to use CryoSpawn on the same host with secure boundaries.
* **Key Tasks:**
  1. Add user identity layer (`users` table with bcrypt password hashing / auth tokens).
  2. Implement Role-Based Access Control (RBAC):
     - **Admin:** Global visibility, quota control, system diagnostics.
     - **Developer / User:** Scoped to their own projects and VMs.
     - **Lab Viewer:** Read-only access to specific lab topologies.
  3. UI Login/Registration screen and session handling.
  4. Project namespaces: VMs and network links belong to an explicit project space owned by users or teams.

---

### Phase 8: CryoSpawn Topology Hub & Dynamic Snapshot Branching
* **Goal:** Transform single VMs into complex, savable, and branchable multi-VM environments.
* **Key Tasks:**
  1. **Topology Snapshots:** Save state of an entire interconnected multi-VM group (e.g., Frontend VM + Database VM + Routing rules) into a single logical "Lab Snapshot".
  2. **VM Branching & Forking:** Take any running or hibernated VM and fork a new instance with Copy-on-Write overlays.
  3. **Topology Blueprints:** Export/import topology configurations as reproducible JSON/YAML definitions.
  4. Interactive canvas/graph view in the UI showing active nodes and links.

---

### Phase 9: Cryptographic Handshakes & Zero-Trust VM Mesh
* **Goal:** Replace raw host iptables links with an authenticated, encrypted inter-VM and inter-user mesh.
* **Key Tasks:**
  1. **Cryptographic Handshake Protocol:**
     - User A generates a cryptographically signed peering token / invite (containing public keys and allowed port policies).
     - User B accepts the token to authorize the handshake.
  2. **Encrypted Overlay Tunnels:**
     - Establish authenticated point-to-point tunnels (using WireGuard keys or Noise-protocol userspace tunneling).
     - No unauthenticated cross-tenant network leakage.
  3. **Cross-Laptop & Remote Peering:**
     - Prepare overlay network encapsulation so VMs on two separate laptops (connected via Tailscale or WAN) peer transparently as if on the same virtual switch.

---

### Phase 10: Host Administrator "God-Mode" Control Panel
* **Goal:** Give the host machine owner omnipotent control, diagnostics, and telemetry.
* **Key Tasks:**
  1. Host Telemetry: Real-time KVM CPU utilization, RAM pressure, active TAP descriptors, and disk usage.
  2. Global Actions:
     - "Emergency Hibernate All" (free up all host RAM in one click).
     - "Nuke Stale Resources" (automated garbage collection for unreferenced sockets, rootfs files, and orphan TAP interfaces).
  3. Real-time audit logs of user actions, API calls, and peer handshakes.
  4. Quota management: Set hard per-user limits for vCPUs, RAM, and total disk footprint.

---

## 5. Implementation Sequence & Next Step

The next immediate task to begin implementation on is:
👉 **Phase 6: Dynamic User-Selectable Disk Sizing (Rootfs Resizing)**
- Enable UI selection of disk sizes.
- Implement sparse file expansion and `resize2fs` in the C++ orchestrator.
- Validate that the VM boots with the expanded ext4 root partition intact.
