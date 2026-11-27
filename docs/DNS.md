# 🌍 Cloudflare Tunnel & Nginx Ingress

> **Goal:** Publish the CryoSpawn dashboard (`cryospawn.<yourdomain>`) and every hosted MicroVM (`cryo-<hex>.<yourdomain>`) over HTTPS with **zero open ports on your home router**.

---

## 🧭 1. Architecture Overview

```text
[ Internet Browser ]
        │  HTTPS (TLS terminated at Cloudflare Edge)
        ▼
[ Cloudflare Edge ]  (Wildcard *.yourdomain.com + cryospawn.yourdomain.com)
        │  Encrypted outbound tunnel (0 router ports opened)
        ▼
[ cloudflared daemon ]  (Running on Host Laptop)
        │  Forwards all traffic to http://127.0.0.1:80
        ▼
[ Host Nginx (:80, nginx/server.sh) ]
        │
        ├── Host = cryospawn.* / app.* / admin.* ──► http://127.0.0.1:8080 (Flask + xterm.js WS)
        │
        └── Host = cryo-<hex>.<domain> ───────────► http://unix:/tmp/cryo-<hex>_80.sock
                                                            │
                                                            ▼
                                                    [ Ghost Proxy ] ──► [ MicroVM 172.16.x.2:80 ]
```

---

## ☁️ 2. Named Cloudflare Tunnel Setup

Using a **named** Cloudflare Tunnel gives you permanent URLs on your own domain (e.g. `zibai.space` or `cryospawn.com`):

1. **Authenticate & create the tunnel:**
   ```bash
   cloudflared tunnel login
   cloudflared tunnel create cryospawn
   ```

2. **Route DNS records in Cloudflare:**
   ```bash
   cloudflared tunnel route dns cryospawn cryospawn.yourdomain.com
   cloudflared tunnel route dns cryospawn "*.yourdomain.com"
   ```
   Verify in Cloudflare Dashboard → **SSL/TLS** that the mode is **Full** (or **Flexible**).

3. **Configure `/etc/cloudflared/config.yml`:**
   ```yaml
   tunnel: <tunnel-id>
   credentials-file: /home/<your-user>/.cloudflared/<tunnel-id>.json

   ingress:
     - hostname: "*.yourdomain.com"
       service: http://127.0.0.1:80
     - hostname: "yourdomain.com"
       service: http://127.0.0.1:80
     - service: http_status:404
   ```

4. **Start `cloudflared` as a service:**
   ```bash
   sudo cloudflared service install
   sudo systemctl enable --now cloudflared
   ```

---

## 🚦 3. Host Nginx Configuration (`nginx/server.sh`)

Run `bash nginx/server.sh` on the host. It installs `/etc/nginx/conf.d/cryospawn.conf` with two server blocks on port `80`:

1. **Platform Ingress (`cryospawn.* app.* admin.*`):** Proxies to `127.0.0.1:8080` with `Upgrade` and `Connection "upgrade"` headers (and `3600s` read/send timeouts) so **`wss://cryospawn.<domain>/ws/console/<uuid>`** (`xterm.js`) stays connected.
2. **Dynamic MicroVM Ingress (`~^(?:cryo-)?(?<vmhex>[a-f0-9]{8})\.(?<domain>.+)$`):** Extracts the 8-character hex ID from any domain and proxies directly to `/tmp/cryo-${vmhex}_80.sock` (also forwarding WebSocket headers).

> [!IMPORTANT]
> On Ubuntu/Debian, `nginx.service` defaults to `PrivateTmp=true`, which hides `/tmp/cryo-*.sock` from Nginx and causes `502 Bad Gateway`. [`nginx/server.sh`](../nginx/server.sh) automatically creates `/etc/systemd/system/nginx.service.d/override.conf` with `PrivateTmp=false` and restarts Nginx.

---

## 🔒 4. Security Checklist

* **Never expose `127.0.0.1:9090` (C++ daemon) or `0.0.0.0:9091` (God-Mode Admin)** in `cloudflared` or Nginx.
* Set `CRYOSPAWN_SECRET_KEY` and `CRYOSPAWN_INTERNAL_SECRET` before starting `frontend/app.py`.
* Set `CRYOSPAWN_PUBLIC_DOMAIN=yourdomain.com` (or simply access the dashboard via `https://cryospawn.yourdomain.com`) so the UI renders `https://cryo-<hex>.yourdomain.com` links automatically.
* Remove or rotate the default `admin` / `admin123` account before exposing the dashboard online.
