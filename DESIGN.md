# CryoSpawn: System Architecture & Software Design Document

Welcome to the **CryoSpawn System Architecture and Design Document**! 

Whether you are a seasoned kernel developer, a frontend engineer, or a curious tech enthusiast who has never touched C++ or Linux networking before, this document explains **how every piece of CryoSpawn works under the hood** in simple, plain English (with helpful analogies, diagrams, and exact code breakdowns).

---

## Table of Contents
1. [The Big Picture: What is CryoSpawn?](#1-the-big-picture-what-is-cryospawn)
2. [Component Breakdown (Who Does What?)](#2-component-breakdown-who-does-what)
3. [Database Design (The System's Memory)](#3-database-design-the-systems-memory)
4. [Networking Architecture: How Packets Travel](#4-networking-architecture-how-packets-travel)
   - [The `/30` Subnet Formula](#the-30-subnet-formula)
   - [Outbound Internet (NAT Masquerade)](#outbound-internet-nat-masquerade)
   - [Zero-Trust Subnet Isolation & Inter-VM Linking](#zero-trust-subnet-isolation--inter-vm-linking)
   - [The Ghost Proxy & Unix Domain Sockets (UDS)](#the-ghost-proxy--unix-domain-sockets-uds)
   - [Nginx Ingress (`cryo-<hex>.cryo`) & Local DNS](#nginx-ingress-cryo-hexcryo--local-dns)
   - [SSH via Unix Socket ProxyCommand](#ssh-via-unix-socket-proxycommand)
5. [End-to-End Data Flows (Step-by-Step)](#5-end-to-end-data-flows-step-by-step)
   - [Flow 1: Creating a MicroVM](#flow-1-creating-a-microvm)
   - [Flow 2: Freezing a VM (Hibernation)](#flow-2-freezing-a-vm-hibernation)
   - [Flow 3: The Magic Auto-Wake (Ghost Proxy in Action)](#flow-3-the-magic-auto-wake-ghost-proxy-in-action)
   - [Flow 4: Destroying a VM & Cleanup](#flow-4-destroying-a-vm--cleanup)
6. [API Reference (HTTP REST Endpoints)](#6-api-reference-http-rest-endpoints)
7. [Core Code Reference: Important Functions](#7-core-code-reference-important-functions)
8. [File System Layout & Artifacts](#8-file-system-layout--artifacts)

---

## 1. The Big Picture: What is CryoSpawn?

Imagine you want to run 20 different Ubuntu Linux servers on your laptop. 
- If you use **VirtualBox** or **VMware**, your laptop will run out of RAM immediately because each virtual machine demands gigabytes of memory and takes 30 to 60 seconds to boot.
- If you use **Docker**, containers share the host Linux kernel, meaning they are not truly isolated virtual hardware machines.

**CryoSpawn** uses **AWS Firecracker** (the same hypervisor engine powering AWS Lambda and Fargate) on top of Linux KVM. 
Instead of booting big, heavy VMs, Firecracker boots **MicroVMs** in **under 150 milliseconds** with minimal overhead.

On top of this engine, CryoSpawn adds our **"Cryo Engine" superpowers**:
1. **Instant Freeze & Thaw (Hibernation):** When a VM is idle, we dump its RAM and CPU registers straight to an SSD snapshot file and kill the process. It consumes **0% CPU and 0 MB RAM**.
2. **Ghost Proxy (Auto-Wake):** When someone sends web traffic to a sleeping VM, CryoSpawn catches the packet in mid-air, restores the VM from SSD in ~80 milliseconds, and delivers the packet seamlessly. To the user browsing the site, it just looks like a fast website!
3. **Zero Host Pollution:** Unlike standard tools that greedily hog TCP ports on your laptop (`8000`, `8080`, `2200`), CryoSpawn uses **Unix Domain Sockets** (`.sock` files) and a single **Nginx Reverse Proxy**. Your laptop stays clean, secure, and unpolluted.

---

## 2. Component Breakdown (Who Does What?)

```
+----------------------------------------------------------------------------+
|                            USER INTERFACE (UI)                             |
|  - Web dashboard (HTML5 / Tailwind CSS / Vanilla JS)                       |
|  - Pages: login, MicroVMs, Elastic Volumes, Routing & Mesh, Settings       |
|  - Real-time polling every 2s for live VM states, IPs, and link buttons    |
+-------------------------------------+--------------------------------------+
                                      | HTTP (:8080, session cookie)
                                      v
+----------------------------------------------------------------------------+
|                    FLASK FRONTEND (Python, frontend/app.py)                |
|  - Login / logout / me, users table in SQLite (hashed passwords)           |
|  - Serves the pages in internal/ui/, redirects to /login without a session |
|  - Forwards /api/* to the daemon and adds X-User-ID and X-Role headers     |
+-------------------------------------+--------------------------------------+
                                      | HTTP REST JSON (127.0.0.1:9090 only)
                                      v
+----------------------------------------------------------------------------+
|                       CRYOSPAWN DAEMON (C++17 Engine)                      |
|                                                                            |
|  [main.cpp]                                                                |
|  - Embedded multi-threaded HTTP server (httplib), REST API only            |
|  - Exposes REST API endpoints (/api/vms, /api/links, etc.)                 |
|                                                                            |
|  [config.hpp]                                                              |
|  - Resolves the project root from the binary's own path                    |
|                                                                            |
|  [orchestrator.hpp]                                                        |
|  - VMManager: Master brain of VM lifecycles                                |
|  - Subprocess forking, state machines ("booting", "running", "hibernated") |
|  - Background Reaper thread (monitors process exits & cleans resources)    |
|                                                                            |
|  [network.hpp]                                                             |
|  - Linux TAP interface creation (ip tuntap, ip addr)                       |
|  - iptables firewall configuration, NAT masquerading, cross-subnet links   |
|                                                                            |
|  [proxy.hpp]                                                               |
|  - Ghost Proxy: Multi-threaded Unix Domain Socket listener                 |
|  - Traffic interceptor & auto-wake caller                                  |
|                                                                            |
|  [database.hpp]                                                            |
|  - SQLite persistence layer (recovers state after daemon reboot)           |
+-------------------+------------------------------------+-------------------+
                    |                                    |
          Fork & Unix Socket API               Unix Domain Sockets (/tmp/*.sock)
                    v                                    v
+--------------------------------------+  +----------------------------------+
|      AWS FIRECRACKER PROCESSES       |  |       HOST NGINX INGRESS         |
|  - KVM virtualization hardware engine|  |  - Wildcard *.cryo routing       |
|  - Loads ubuntu-vmlinux.bin kernel   |  |  - Passes traffic to Ghost Proxy |
|  - Mounts instance Copy-on-Write ext4|  +----------------------------------+
+--------------------------------------+
```

---

## 3. Database Design (The System's Memory)

CryoSpawn uses an embedded **SQLite** database (`cryospawn.db`). If the daemon crashes or your laptop restarts, CryoSpawn looks at this database to immediately re-adopt running VMs, know which IPs belong to whom, and restore all proxy listeners.

### Table: `vms`
Stores every individual MicroVM registered in the platform.

| Column Name | SQL Type | Description / Purpose |
| :--- | :--- | :--- |
| `slot_id` | `INTEGER PRIMARY KEY` | The internal slot (0, 1, 2...). Used for the IP and MAC formulas. Not exposed as an API identifier. |
| `uuid` | `TEXT` | The public VM ID, `vm-` plus 8 hex characters (e.g. `vm-da05a06c`). Used by the API, TAP name, sockets and disk file names. Legacy rows get one on startup. |
| `name` | `TEXT` | Human-readable name (legacy rows become `legacy-vm-<slot>`). |
| `owner_id` | `INTEGER DEFAULT 1` | The `users.id` of the owner. Admins see every VM, other users only their own. |
| `pid` | `INTEGER` | The Linux Process ID (PID) of the active Firecracker instance. Set to `0` when hibernated. |
| `status` | `TEXT` | Current lifecycle state: `"booting"`, `"running"`, `"hibernated"`, `"waking"` or `"restarting"`. |
| `vcpus` | `INTEGER` | Number of virtual CPU cores allocated (e.g., 1 or 2). |
| `mem_mib` | `INTEGER` | Dedicated RAM in Megabytes (e.g., 512, 1024). |
| `guest_ip` | `TEXT` | The internal IP address of the VM inside its virtual network (e.g., `172.16.0.2`). |
| `host_ip` | `TEXT` | The host laptop's gateway IP on the matching TAP interface (e.g., `172.16.0.1`). |
| `tap_name` | `TEXT` | The name of the virtual network card on the host (e.g., `cryo-da05a06c`, short enough for the 15-character Linux interface name limit). |
| `ssh_exposed`| `INTEGER` | Boolean (`1` or `0`): whether Ghost Proxy should listen for SSH (Port 22). |
| `http_exposed`| `INTEGER` | Boolean (`1` or `0`): whether Ghost Proxy should listen for HTTP (Port 80). |
| `https_exposed`| `INTEGER`| Boolean (`1` or `0`): whether Ghost Proxy should listen for HTTPS (Port 443). |
| `host_port` | `INTEGER` | Legacy port field (kept for schema backward compatibility). |
| `project_name` | `TEXT` | The project namespace (e.g., `"default"`, `"production"`, `"lab1"`). |
| `rootfs_gb` | `INTEGER` | Requested root disk size in GB (default `1` in the schema; the API defaults to `3`). Added by a safe `ALTER TABLE`. |

### Table: `network_links`
Stores custom virtual network bridges established between two isolated VMs.

| Column Name | SQL Type | Description / Purpose |
| :--- | :--- | :--- |
| `id` | `INTEGER PRIMARY KEY AUTOINCREMENT` | Auto-incrementing link ID. |
| `owner_id` | `INTEGER DEFAULT 1` | Owner of the link. |
| `project_name`| `TEXT` | Namespace tag for the connection. |
| `vm1_id` | `INTEGER` | Slot of the first VM (the API translates it to and from UUIDs). |
| `vm2_id` | `INTEGER` | Slot of the second VM. |
| *Constraint* | `UNIQUE(vm1_id, vm2_id)` | Prevents duplicate routing rules between the same two VMs. |

### Table: `volumes`
Stores extra ext4 disks (Elastic Storage Volumes) that can be attached to a VM.

| Column Name | SQL Type | Description / Purpose |
| :--- | :--- | :--- |
| `id` | `INTEGER PRIMARY KEY AUTOINCREMENT` | Volume ID. The disk file is `volumes/vol_<id>.ext4`. |
| `owner_id` | `INTEGER DEFAULT 1` | Owner of the volume. |
| `uuid` | `TEXT UNIQUE` | Reserved for a future volume UUID; not filled in yet. |
| `name` | `TEXT` | Human-readable name. |
| `size_gb` | `INTEGER` | Size in GB. |
| `attached_vm_id` | `INTEGER DEFAULT -1` | Slot of the VM using the volume, or `-1` when detached. |

Volumes are created with `truncate` + `mkfs.ext4`. `configure_and_start()` attaches every volume whose `attached_vm_id` matches the VM's slot as an extra Firecracker drive (`PUT /drives/vol_<id>`). Attaching or detaching reboots a running VM so the drive change takes effect.

### Table: `users` (owned by the Flask frontend)
Created by `frontend/app.py` in the same `cryospawn.db` file. The daemon never reads it; it only receives the user's ID and role as headers.

| Column Name | SQL Type | Description / Purpose |
| :--- | :--- | :--- |
| `id` | `INTEGER PRIMARY KEY AUTOINCREMENT` | User ID, passed to the daemon as `X-User-ID`. |
| `username` | `TEXT UNIQUE NOT NULL` | Login name. |
| `password_hash` | `TEXT NOT NULL` | Werkzeug password hash. |
| `role` | `TEXT DEFAULT 'user'` | `admin` or `user`, passed to the daemon as `X-Role`. |

When the table is empty, a first start creates the user `admin` with a default password (see [docs/SETUP.md](docs/SETUP.md#-first-login)).

---

## 4. Networking Architecture: How Packets Travel

Networking in Firecracker requires careful isolation. Here is how CryoSpawn constructs the entire network fabric without bridges or collisions.

```
       [ Internet (Wi-Fi: wlo1 / Ethernet: eth0) ]
                            ^
                            | (NAT / MASQUERADE)
           [ Host Laptop Linux Kernel (iptables) ]
             |                                 |
       TAP: cryo-<hex0>                   TAP: cryo-<hex1>
       Host IP: 172.16.0.1/30            Host IP: 172.16.0.5/30
             | (Virtual Wire)                  | (Virtual Wire)
             v                                 v
       Guest Eth0: 172.16.0.2            Guest Eth0: 172.16.0.6
        [ MicroVM 0 ]                     [ MicroVM 1 ]
```

### The `/30` Subnet Formula
To guarantee that no two VMs can ever clash or eavesdrop on each other, every VM gets its own private `/30` point-to-point subnet (which contains exactly **4 IP addresses**: 1 network, 1 host gateway, 1 guest VM, 1 broadcast).

The IP addresses are calculated with pure math based on the VM's slot:
```cpp
int base = slot * 4;
// Host Laptop TAP Gateway IP:
vm.host_ip = "172.16." + to_string(base / 256) + "." + to_string((base % 256) + 1);

// Guest MicroVM IP:
vm.ip      = "172.16." + to_string(base / 256) + "." + to_string((base % 256) + 2);
```

#### Example IP Mapping Table:
| Slot | Subnet CIDR | Host Gateway (TAP) | MicroVM Internal IP |
| :---: | :---: | :---: | :---: |
| **0** | `172.16.0.0/30` | `172.16.0.1` | `172.16.0.2` |
| **1** | `172.16.0.4/30` | `172.16.0.5` | `172.16.0.6` |
| **2** | `172.16.0.8/30` | `172.16.0.9` | `172.16.0.10` |
| **...** | ... | ... | ... |
| **64** | `172.16.1.0/30` | `172.16.1.1` | `172.16.1.2` |

Each VM is given a unique MAC address formatted as:
`AA:FC:AC:10:XX:YY` (where `AC:10` is hex for `172.16`, and `XX:YY` is the slot in hex).

---

### Outbound Internet (NAT Masquerade)
How does a VM ping `google.com`?
1. The VM sends a packet with destination `8.8.8.8` to its default gateway (`172.16.0.1`).
2. The packet hits the VM's virtual TAP device (`cryo-<hex>`) on the host.
3. Linux Kernel IP Forwarding (`net.ipv4.ip_forward = 1`) allows the packet to jump to your physical Wi-Fi interface (`wlo1`).
4. An `iptables` rule applies **NAT Masquerade**:
   ```bash
   sudo iptables -t nat -A POSTROUTING -o wlo1 -j MASQUERADE
   sudo iptables -A FORWARD -i cryo-<hex> -o wlo1 -j ACCEPT
   ```
   The packet's source IP is rewritten to your home router's Wi-Fi IP. When Google responds, Linux rewrites it back and forwards it to `172.16.0.2`.

---

### Zero-Trust Subnet Isolation & Inter-VM Linking
By default, **VMs are completely deaf and blind to each other**.
In `network.hpp`, the baseline firewall drops any packet trying to hop between `cryo` interfaces:
```bash
sudo iptables -A FORWARD -i cryo+ -o cryo+ -d 172.16.0.0/16 -j DROP
```

When you link VM 0 and VM 1 (same project) on the **Routing & Mesh** page, CryoSpawn inserts high-priority reciprocal forward and masquerade rules:
```bash
sudo iptables -I FORWARD 1 -i cryo-<hex0> -o cryo-<hex1> -j ACCEPT
sudo iptables -I FORWARD 1 -i cryo-<hex1> -o cryo-<hex0> -j ACCEPT
sudo iptables -t nat -I POSTROUTING 1 -s 172.16.0.0/16 -o cryo-<hex1> -j MASQUERADE
sudo iptables -t nat -I POSTROUTING 1 -s 172.16.0.0/16 -o cryo-<hex0> -j MASQUERADE
```
Now—and only now—can VM 0 ping or communicate with VM 1!

The baseline also protects the host and your LAN: forwarded traffic from `cryo+` interfaces to `192.168.0.0/16` and `10.0.0.0/8` is rejected, and `INPUT` from `cryo+` is rejected except for replies, so a VM cannot connect to services on the hypervisor itself.

---

### The Ghost Proxy & Unix Domain Sockets (UDS)

#### What is a Unix Domain Socket?
Normally, when programs talk to each other over a network, they use TCP ports (`localhost:8080`). But TCP ports:
- Pollute the host's port namespace.
- Can collide with other programs you have running.
- Can be probed by other devices on your Wi-Fi network.

A **Unix Domain Socket** is a file on your disk (like `/tmp/cryo-da05a06c_80.sock`) that behaves like a network connection. It has no port number, runs completely inside kernel memory, is faster than TCP, and is 100% isolated to your machine.

#### How Ghost Proxy Works
For every exposed service on a VM, CryoSpawn spins up a lightweight C++ background thread running `run_ghost_proxy()`:
- Port 22 -> `/tmp/cryo-<hex>_22.sock`
- Port 80 -> `/tmp/cryo-<hex>_80.sock`
- Port 443 -> `/tmp/cryo-<hex>_443.sock`

The proxy thread sits on `accept()`. When any byte arrives on that socket:
1. It queries: *"Is this VM hibernated?"*
2. If **yes**: It triggers `wake_vm()`, restoring the Firecracker process in ~80ms.
3. Once running: It establishes a TCP socket to `172.16.0.2:<port>` and pumps the bytes bidirectionally between the Unix socket and the VM!

---

### Nginx Ingress (`cryo-<hex>.cryo`) & Local DNS

How does typing `http://cryo-da05a06c.cryo` in your browser hit the right Unix Socket?

```
[ Browser: http://cryo-da05a06c.cryo ]
           |
           v (Localhost 127.0.0.1 via /etc/hosts)
[ Host Nginx Port 80 ]
  Rule: server_name ~^cryo-(?<hex>[a-f0-9]+)\.cryo$;
           |
           v Extracts $hex = da05a06c
  proxy_pass http://unix:/tmp/cryo-da05a06c_80.sock;
           |
           v
[ Ghost Proxy ] ---> (Wakes VM if needed) ---> [ VM Internal Web Server ]
```

1. **Local DNS:** Your `/etc/hosts` file contains:
   ```
   127.0.0.1 cryo-da05a06c.cryo
   ```
   When you browse to `cryo-da05a06c.cryo`, your laptop resolves it to `127.0.0.1` (itself).
2. **Nginx Wildcard Regex:** In `/etc/nginx/conf.d/cryospawn.conf`:
   ```nginx
   server {
       listen 80;
       server_name ~^cryo-(?<hex>[a-f0-9]+)\.cryo$;

       location / {
           proxy_pass http://unix:/tmp/cryo-${hex}_80.sock;
           proxy_set_header Host $host;
           proxy_set_header X-Real-IP $remote_addr;
           proxy_http_version 1.1;
       }
   }
   ```
   Nginx captures the hex part of the domain name (e.g. `cryo-da05a06c` -> `hex = da05a06c`) and automatically passes the connection directly into `/tmp/cryo-da05a06c_80.sock`!

---

### SSH via Unix Socket ProxyCommand
Because SSH does not have HTTP host headers, how do you SSH into a VM without opening a host TCP port?
Using OpenSSH's built-in **`ProxyCommand`** and `netcat` (`nc`):

```bash
ssh -o ProxyCommand="nc -U /tmp/cryo-da05a06c_22.sock" root@localhost
```

**What this command does:**
- `ssh` starts up on your laptop.
- Instead of opening a TCP connection to port 22, it executes `nc -U /tmp/cryo-da05a06c_22.sock`.
- Netcat connects directly to the Unix Domain Socket.
- Ghost Proxy detects the connection, wakes the VM if it was sleeping, and connects to the VM's internal SSH server (`172.16.0.2:22`).
- You are logged into root shell!

---

## 5. End-to-End Data Flows (Step-by-Step)

### VM lifecycle states

```mermaid
stateDiagram-v2
    [*] --> booting: create_vm
    booting --> running: InstanceStart ok
    running --> hibernated: hibernate_vm
    hibernated --> waking: wake_vm (API or Ghost Proxy)
    waking --> running: snapshot loaded and resumed
    running --> booting: reboot_vm (volume attach/detach)
    running --> [*]: terminate_vm
    hibernated --> [*]: terminate_vm
```

### Flow 1: Creating a MicroVM
When you click **"Deploy MicroVM"** in the web dashboard:

```
[UI: Submit Form]
       |
       v (POST /api/vms {"name": "web-1", "vcpus": 1, "mem_mib": 512, "expose_http": true, ...})
[frontend/app.py: checks the session, adds X-User-ID / X-Role, forwards to :9090]
       |
       v
[main.cpp: svr.Post("/api/vms")]
       |
       v
[orchestrator.hpp: create_vm()]
       |-- 1. db.get_next_free_slot() -> Finds slot 0, generates UUID vm-da05a06c
       |-- 2. Calculates IPs (172.16.0.1 / 172.16.0.2) and MAC address
       |-- 3. setup_cryo_network() -> Creates tap device `cryo-da05a06c`, assigns IP, sets up NAT
       |-- 4. fork() -> Starts new `firecracker --api-sock /tmp/cryo-da05a06c.socket`
       |-- 5. db.insert_vm() -> Records VM in SQLite
       v
[orchestrator.hpp: configure_and_start()] (runs in background thread)
       |-- 1. Waits for /tmp/cryo-da05a06c.socket to appear
       |-- 2. PUT /boot-source (passes ubuntu-vmlinux.bin + kernel boot args)
       |-- 3. Copies rootfs/ubuntu-rootfs.ext4 -> instances/cryo-da05a06c_rootfs.ext4 (private copy per VM)
       |      If rootfs_gb >= 3: truncate -s <N>G, e2fsck -fy, resize2fs (grows the disk)
       |-- 4. PUT /drives/rootfs (attaches instances/cryo-da05a06c_rootfs.ext4)
       |-- 5. PUT /drives/vol_<id> for every volume attached to this slot
       |-- 6. PUT /network-interfaces/eth0 (attaches tap device cryo-da05a06c)
       |-- 7. PUT /machine-config (sets vCPUs and RAM)
       |-- 8. PUT /actions {"action_type": "InstanceStart"} -> KVM executes Linux kernel!
       v
[main.cpp: Spawns Ghost Proxy Threads]
       |-- Spawns thread running run_ghost_proxy("/tmp/cryo-da05a06c_80.sock", ...)
       |-- Spawns thread running run_ghost_proxy("/tmp/cryo-da05a06c_22.sock", ...)
       v
[MicroVM is Running and Ready!]
```

---

### Flow 2: Freezing a VM (Hibernation)
When you click **"Hibernate"** (or auto-idle triggers):

```
[UI: Hibernate Button]
       |
       v (POST /api/vms/vm-da05a06c/hibernate)
[orchestrator.hpp: hibernate_vm(slot 0)]
       |-- 1. Acquires std::lock_guard<std::mutex> lock(mtx)
       |-- 2. PATCH /vm {"state": "Paused"} -> Instantly freezes VM CPU execution
       |-- 3. PUT /snapshot/create {
       |         "snapshot_path": "instances/cryo-da05a06c_state.snap",
       |         "mem_file_path": "instances/cryo-da05a06c_mem.ram"
       |      } -> Dumps full RAM and CPU state to disk
       |-- 4. Sets vms[0].status = "hibernated" & updates SQLite
       |-- 5. kill(vms[0].pid, SIGKILL) -> Kills Firecracker process
       v
[Reaper Thread wakes up from waitpid()]
       |-- Sees process exited
       |-- Checks: status == "hibernated"?
       |-- YES: Sets pid = 0, LEAVES DISK AND SNAPSHOT FILES FULLY INTACT!
       v
[VM uses 0 MB RAM and 0% CPU on Host!]
```

---

### Flow 3: The Magic Auto-Wake (Ghost Proxy in Action)
The VM is hibernated. You open your browser and navigate to `http://cryo-da05a06c.cryo`:

```mermaid
sequenceDiagram
    participant B as Browser
    participant N as Nginx
    participant P as Ghost Proxy
    participant M as VMManager
    participant F as Firecracker
    participant V as VM web server

    B->>N: GET http://cryo-da05a06c.cryo
    N->>P: /tmp/cryo-da05a06c_80.sock
    P->>P: status_fn(slot) is hibernated
    P->>M: wake_vm(slot)
    M->>M: status = "waking" (lock blocks concurrent wakes)
    M->>F: fork() new firecracker process
    M->>F: PUT /snapshot/load (state + memory files)
    M->>F: PATCH /vm {"state": "Resumed"}
    M->>M: status = "running"
    M-->>P: true
    P->>V: TCP connect 172.16.0.2:80
    P->>V: relay GET request
    V-->>P: response
    P-->>N: relay response
    N-->>B: page (about 80 to 120 ms total)
```

---

### Flow 4: Destroying a VM & Cleanup
When you click **"Destroy"** in the UI:

```
[UI: Destroy Button]
       |
       v (DELETE /api/vms/vm-da05a06c)
[orchestrator.hpp: terminate_vm(slot 0)]
       |-- 1. Sends SIGKILL to the Firecracker PID (if running); the Reaper then runs cleanup
       |-- 2. cleanup_vm_resources(vm):
       |      - unlinks Firecracker API socket (/tmp/cryo-da05a06c.socket)
       |      - unlinks instances/cryo-da05a06c_rootfs.ext4 (frees SSD disk space)
       |      - unlinks instances/cryo-da05a06c_state.snap (frees snapshot space)
       |      - unlinks instances/cryo-da05a06c_mem.ram (frees memory dump)
       |      - unlinks /tmp/cryo-da05a06c_22.sock (cleans Ghost Proxy)
       |      - unlinks /tmp/cryo-da05a06c_80.sock (cleans Ghost Proxy)
       |      - unlinks /tmp/cryo-da05a06c_443.sock (cleans Ghost Proxy)
       |      - teardown_cryo_network("cryo-da05a06c"): deletes TAP interface & iptables rules
       |      - db.remove_vm(0): Deletes record from SQLite
       v
[System is 100% pristine. Zero leftover files, zero zombie processes.]
```

---

## 6. API Reference (HTTP REST Endpoints)

Clients talk to the Flask frontend on `http://0.0.0.0:8080`, which handles login and forwards `/api/*` to the C++ daemon on `127.0.0.1:9090`. The daemon exposes endpoints for VMs (`/api/vms`, addressed by UUID), network links (`/api/links`) and volumes (`/api/volumes`).

The full endpoint reference, with request and response fields, lives in [docs/API.md](docs/API.md).

---

## 7. Core Code Reference: Important Functions

### In `internal/orchestrator.hpp`:
- `create_vm(vcpus, mem_mib, expose_ssh, expose_http, expose_https, project, rootfs_gb, name)`: Picks the next free slot, generates the VM UUID, allocates IP/MAC addresses, sets up the TAP interface, and forks the Firecracker subprocess.
- `reboot_vm(id, kernel, rootfs)`: Kills and respawns Firecracker for a VM while keeping its TAP and disk (used by `restart` and by volume attach/detach).
- `get_slot_by_uuid(uuid)`: Translates the public UUID into the internal slot.
- `configure_and_start(id, kernel, rootfs)`: Talks to Firecracker's REST socket to configure virtual hardware (kernel, Copy-on-Write rootfs, network interface, vCPUs, RAM) and issues `InstanceStart`.
- `hibernate_vm(id)`: Freezes CPU execution (`PATCH /vm Paused`), dumps memory and snapshot files to disk (`PUT /snapshot/create`), updates database status, and kills the Firecracker process.
- `wake_vm(id)`: Handles thread-safe auto-waking: marks status as `"waking"`, forks a fresh Firecracker process, loads the snapshot files (`PUT /snapshot/load`), and resumes execution (`PATCH /vm Resumed`).
- `start_reaper()`: Background thread running `waitpid()` in an infinite loop. Cleans up crashed or terminated processes while protecting hibernated snapshot files.
- `reconcile_state()`: Runs on daemon startup. Scans the SQLite database, checks if previously registered PIDs are alive, adopts them into memory, and restarts their Ghost Proxies.

### In `internal/proxy.hpp`:
- `run_ghost_proxy(uds_path, vm_ip, vm_port, id, wake_fn, status_fn)`: Creates and binds an `AF_UNIX` stream socket on disk (`chmod 0666` so Nginx can access it). Loops on `accept()` and spawns detached client handler threads.
- `handle_proxy_client(client_sock, vm_ip, vm_port, id, wake_fn, status_fn)`: Checks if the target VM is currently asleep. If so, triggers `wake_vm(id)` and waits for it to become ready. Then connects to the VM's internal IP:Port and launches bidirectional data relays.
- `relay_data(sock1, sock2)`: High-performance bidirectional streaming loop that pipes raw bytes between the Unix socket and the VM TCP socket.

### In `internal/config.hpp`:
- `config::BASE_PATH`: The project root, found by reading `/proc/self/exe`. The kernel, template disk, `instances/` and `volumes/` are all resolved from it.

### In `frontend/app.py`:
- `init_db()`: Creates the `users` table and the default `admin` user.
- `/api/auth/login`, `/api/auth/logout`, `/api/auth/me`: Session handling.
- `proxy_api(path)`: Requires a session, then forwards the request to the daemon with trusted `X-User-ID` and `X-Role` headers.

### In `internal/network.hpp`:
- `init_cryo_firewall_baseline(out_iface)`: Configures host-wide IP forwarding, sets up NAT masquerade, installs the default zero-trust drop rule to isolate all `/30` subnets, and blocks VMs from the host's LAN and from the hypervisor's own services.
- `setup_cryo_network(tap_name, host_ip, out_iface)`: Uses `ip tuntap add` to create a virtual network card for the VM and assigns the host gateway IP.
- `link_taps(tap1, tap2)` / `unlink_taps(tap1, tap2)`: Injects/removes bidirectional `iptables FORWARD` and `MASQUERADE` rules to dynamically allow two isolated VMs to communicate.

---

## 8. File System Layout & Artifacts

```
CryoSpawn/
├── cryospawn.db                   # SQLite database (vms, network_links, volumes, users)
├── README.md                      # Project overview & documentation guide
├── ROADMAP.md                     # Platform vision & phased engineering roadmap
├── DESIGN.md                      # This document (Architecture & Design)
├── docs/                          # SETUP.md, API.md, DNS.md, KNOWN-ISSUES.md
├── frontend/
│   └── app.py                    # Flask frontend: login, sessions, page serving, API forwarding
├── volumes/                       # Attachable disks: vol_<id>.ext4
├── instances/                     # Per-VM dynamic instance files
│   ├── cryo-da05a06c_rootfs.ext4  # Private root disk for VM vm-da05a06c
│   ├── cryo-da05a06c_state.snap   # CPU registers & device state snapshot
│   └── cryo-da05a06c_mem.ram      # RAM memory snapshot file
├── internal/                      # C++ daemon source code
│   ├── main.cpp                  # Daemon entry point & REST API (127.0.0.1:9090)
│   ├── config.hpp                # Project root resolution
│   ├── orchestrator.hpp          # MicroVM lifecycle engine & process manager
│   ├── network.hpp               # Network virtualization & iptables rules
│   ├── proxy.hpp                 # Ghost Proxy Unix Domain Socket engine
│   ├── database.hpp              # SQLite persistence layer
│   ├── lib/
│   │   ├── httplib.h             # Single-header C++ HTTP server library
│   │   └── json.hpp              # Single-header nlohmann JSON library
│   └── ui/                       # Web dashboard files served by Flask
│       ├── login.html            # Login page
│       ├── dashboard.html        # MicroVMs page
│       ├── volumes.html          # Elastic Volumes page
│       ├── networks.html         # Routing & Mesh page
│       ├── settings.html         # Account settings page
│       └── app.js                # Frontend state management & API polling
├── rootfs/
│   └── ubuntu-rootfs.ext4        # Golden template rootfs disk image
├── vmlinux/
│   └── ubuntu-vmlinux.bin        # Uncompressed Linux kernel binary
├── utils/
│   └── start_vm_server.sh        # Guest test script for HTTP/HTTPS web servers
└── /tmp/ (System Temporary Directory)
    ├── cryo-da05a06c.socket          # Firecracker API control socket for the VM
    ├── cryo-da05a06c_22.sock         # Ghost Proxy Unix Socket for SSH
    ├── cryo-da05a06c_80.sock         # Ghost Proxy Unix Socket for HTTP
    └── cryo-da05a06c_443.sock        # Ghost Proxy Unix Socket for HTTPS
```
