# CryoSpawn DNS & Ingress Architecture

This document outlines the production ingress architecture for CryoSpawn. By leveraging Cloudflare Zero Trust Tunnels and a local Nginx reverse proxy, CryoSpawn can host an infinite number of microVMs on a single physical host without exposing any open ports to the public internet.

## 1. Architecture Overview

```text
[ Internet ] 
    |
    v
[ Cloudflare Edge ] (Handles DNS, SSL/TLS, and DDoS Protection)
    |
    | (Encrypted Outbound Tunnel - 0 Open Ports)
    v
[ cloudflared daemon ] (Running on Host Machine)
    |
    | (Forwards all *.yourdomain.com traffic to port 80)
    v
[ Nginx Reverse Proxy ] (The Local Traffic Cop)
    |
    |--- If `app.yourdomain.com`   --> Forwards to Python Flask (Port 8080)
    |--- If `admin.yourdomain.com` --> Forwards to Python Flask (Port 8080)
    |--- If `[uuid].yourdomain.com`--> Pipes directly into Unix Socket (`/tmp/cryo-[uuid]_80.sock`)
```

## 2. URL Schema & Routing Map

The system is strictly designed to use **one level of subdomains** to remain compatible with Cloudflare's Free Universal SSL. 

| URL | Component | Auth Required? | Handled By |
| :--- | :--- | :--- | :--- |
| `yourdomain.com` | Public Marketing Landing Page | No | Nginx / Flask |
| `app.yourdomain.com/login` | Authentication Portal | No | Flask (8080) |
| `app.yourdomain.com/dashboard` | User Workspace (VMs, Volumes) | Yes (Session) | Flask (8080) |
| `app.yourdomain.com/console/[uuid]` | In-Browser Web Terminal | Yes (Session) | Flask -> Unix Socket |
| `admin.yourdomain.com` | Super Admin Global Dashboard | Yes (Admin) | Flask (8080) |
| `[uuid].yourdomain.com` | User's hosted microVM services | No | Nginx -> Unix Socket |

## 3. Cloudflare Configuration (Free Tier)

CryoSpawn utilizes **Wildcard Tunnels** to dynamically route user VMs without requiring API calls to Cloudflare for every new VM.

1. **The Tunnel Route:** A single route is created in `cloudflared` mapping `*.yourdomain.com` to `http://localhost:80`.
2. **The DNS Record:** A manual `CNAME` record with the name `*` must be pointed to the Cloudflare Tunnel ID (e.g., `1234abcd-1234.cfargotunnel.com`).
3. **SSL Limitations:** Cloudflare Free SSL covers the apex (`domain.com`) and one subdomain level (`*.domain.com`). We avoid deep subdomains (like `[uuid].vms.domain.com`) to prevent SSL certificate warnings.

## 4. The Nginx Traffic Cop

Because Cloudflare dumps *all* traffic onto localhost port 80, Nginx is responsible for routing. Nginx reads the `Host` header and applies the following logic:

### Platform Routing (Python Flask)
```nginx
server {
    listen 80;
    server_name app.yourdomain.com admin.yourdomain.com;
    
    location / {
        proxy_pass http://127.0.0.1:8080;
        proxy_set_header Host $host;
    }
}
```

### MicroVM Routing (Dynamic Unix Sockets)
By using regex, Nginx dynamically maps any 8-character hex subdomain to its corresponding Firecracker Unix socket. No Nginx reloads are required when new VMs are created.
```nginx
server {
    listen 80;
    server_name ~^([a-f0-9]{8})\.yourdomain\.com$;

    location / {
        proxy_pass http://unix:/tmp/cryo-$1_80.sock;
        proxy_set_header Host $host;
    }
}
```

## 5. Security Posture
* **Zero Public Ports:** The host machine requires no port forwarding.
* **Namespace Isolation:** System dashboards (`app.`) and user VMs (`[uuid].`) are strictly separated by subdomain names. A user cannot hijack `app.yourdomain.com` because UUIDs are strictly enforced as 8-character hex strings by the C++ engine.
* **WebSocket Authentication:** Terminal access via `app.yourdomain.com/console/[uuid]` is verified by Python session cookies before establishing the PTY stream to the VM.

## 6. Integration with CryoSpawn Engineering Roadmap

This Cloudflare/Nginx Ingress architecture perfectly sets the stage for the final phases of the `ROADMAP.md`:

### Phase 7: Multi-Tenant RBAC
* **Ingress Mapping:** `app.yourdomain.com/login` and `/dashboard`.
* **Execution:** Python will issue secure Session Cookies. Nginx blindly forwards traffic to Python, and Python uses the SQLite `users` table to enforce tenant isolation (User A only sees User A's VMs).

### Phase 8: Topology Hub & Branching
* **Ingress Mapping:** `app.yourdomain.com/topology`.
* **Execution:** Users will interact with the interactive canvas/graph showing active nodes and packet flows. The frontend will hit Python APIs to trigger "Topology Snapshots" and "VM Branching". (The Web Console we discussed earlier can also be integrated here: clicking a node in the topology opens the `xterm.js` terminal window).

### Phase 9: Cryptographic Handshakes & WAN Expansion
* **Ingress Mapping:** Backend Cloudflare API orchestration & Tunnel-to-Tunnel peering.
* **Execution:** Because the platform is fully internet-accessible via Cloudflare Tunnels, Cross-Physical Host Peering (WAN Expansion) becomes natively possible. Two laptops running CryoSpawn can establish authenticated Zero-Trust WireGuard tunnels by exchanging cryptographic tokens via the public API endpoints.

### Phase 10: Host Administrator "God-Mode" Panel
* **Ingress Mapping:** `admin.yourdomain.com` (or `app.yourdomain.com/admin`).
* **Execution:** Secure, globally-scoped route where the Super Admin can execute "Emergency Hibernate All", view KVM telemetry, and manage Hard Quotas. Standard users attempting to access this domain/path will receive a 403 Forbidden via Python.
