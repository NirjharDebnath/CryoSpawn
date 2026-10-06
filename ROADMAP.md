# CryoSpawn: Micro-Cloud Orchestrator
**Engineering Roadmap & Architecture**

## 1. Core Philosophy & Ambition
CryoSpawn is designed to bridge the gap between Docker's speed and KVM's security. By orchestrating AWS Firecracker microVMs over raw Unix sockets and utilizing Copy-on-Write storage, CryoSpawn provides a true, multi-tenant Cloud Infrastructure-as-a-Service (IaaS) capable of running on a single bare-metal host. 

**Pillars of the Architecture:**
- **Micro-Second Telemetry & Boot:** Direct-kernel boots bypassing traditional BIOS/UEFI bloat.
- **Dynamic Sparse Storage:** User-selectable disk sizes with copy-on-write storage isolation.
- **Zero-Trust VM Mesh Peering:** Cryptographically authenticated handshakes for inter-tenant VM networking.
- **Topology Lab Hub:** One-click snapshotting, cloning, and branching of complex multi-VM network graphs.
- **Multi-Tenant RBAC & Host "God-Mode":** Strict permission boundaries and logical namespaces, overseen by an omnipotent host administrative dashboard.

---

## 2. Architecture & State Overview

```text
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
  - Resilient daemon state reconciliation: adopted VMs preserve hibernation status.
- [x] **Phase 5: Guest Test Suite**
  - Scripted guest web server setup for testing auto-wake workflows.

---

## 4. Engineering Roadmap: Next Phases

```text
   [x] Phase 6: Dynamic Disk Sizing
                 |
                 v
   [Phase 7: Multi-Tenant Auth & RBAC]
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

### [COMPLETED] Phase 6: Dynamic User-Selectable Disk Sizing (Rootfs Resizing)
* **Goal:** Allow users to choose custom virtual disk capacity at creation time.
* **Key Tasks:**
  - Added UI slider for precise gigabyte allocation.
  - Integrated smart sparse-file truncation (`truncate -s`).
  - Auto-expand filesystem structure (`e2fsck` + `resize2fs`) while enforcing base-image math.

---

### Phase 7: Multi-Tenant Authentication & Project Scoping (RBAC)
* **Goal:** Enable multiple users to utilize CryoSpawn concurrently with strictly enforced security boundaries.
* **Key Tasks:**
  1. Add an identity and session layer (`users` table with bcrypt password hashing and secure token generation).
  2. Implement Role-Based Access Control (RBAC):
     - **Admin:** Global visibility, quota control, deep system diagnostics.
     - **Tenant / User:** Strictly scoped to their own namespaces, isolated projects, and personal VMs.
     - **Auditor:** Read-only access to topologies and logs.
  3. UI Login/Registration screen and secure cookie/JWT session handling.
  4. Project namespaces: Network links and VMs are cryptographically tied to the owning user's projects.

---

### Phase 8: CryoSpawn Topology Hub & Dynamic Snapshot Branching
* **Goal:** Evolve beyond single VMs into interconnected, savable, and branchable multi-node environments.
* **Key Tasks:**
  1. **Topology Snapshots:** Freeze the state of an entire interconnected multi-VM group (e.g., Frontend VM + DB VM + Routing rules) into a single logical "Environment Blueprint".
  2. **VM Branching & Forking:** Take any running VM and fork a new instance seamlessly using advanced Copy-on-Write overlays.
  3. **Infrastructure-as-Code (IaC):** Export/import topology configurations as highly reproducible JSON/YAML definitions.
  4. Interactive canvas/graph view in the frontend UI showing active nodes, packet flows, and routing links.

---

### Phase 9: Cryptographic Handshakes & Zero-Trust VM Mesh
* **Goal:** Replace standard iptables routing with an authenticated, cryptographically secure inter-tenant mesh.
* **Key Tasks:**
  1. **Cryptographic Handshake Protocol:**
     - User A generates a cryptographically signed peering token (containing public keys, expiration, and strict port policies).
     - User B ingests the token to mathematically authorize the handshake.
  2. **Encrypted Overlay Tunnels:**
     - Establish authenticated point-to-point tunnels via WireGuard or Noise-protocol userspace tunneling.
     - Absolute prevention of unauthenticated cross-tenant network leakage or spoofing.
  3. **Cross-Physical Host Peering (WAN Expansion):**
     - Prepare the overlay encapsulation engine so VMs on two entirely separate physical laptops (connected via WAN/Tailscale) peer transparently as if on the same virtual switch.

---

### Phase 10: Host Administrator "God-Mode" Control Panel
* **Goal:** Grant the bare-metal host owner omnipotent control, advanced diagnostics, and hypervisor-level telemetry.
* **Key Tasks:**
  1. Host Telemetry: Real-time KVM CPU utilization, RAM pressure, active TAP descriptors, and disk footprint metrics.
  2. Global Execution Commands:
     - "Emergency Hibernate All" (Suspend all VMs to disk instantly to free host RAM).
     - "Nuke Stale Resources" (Automated garbage collection for orphaned sockets, volumes, and TAP interfaces).
  3. Real-time security audit logs of user actions, API calls, and peer handshakes.
  4. Hard Quota Management: Enforce absolute maximum limits for vCPUs, memory, and total disk allocation per tenant.

---

## 5. Implementation Sequence & Next Step

The next immediate technological milestone to execute is:
👉 **Phase 7: Multi-Tenant Authentication & Project Scoping (RBAC)**
- Introduce SQLite-backed user credentials and hashing.
- Build the frontend authentication gate.
- Refactor the API endpoints to enforce tenant visibility and authorization token verification.
