#!/bin/bash
set -e

# Default to port 80 if not provided
PORT="${1:-80}"

echo "========================================="
echo "   CryoSpawn Guest Webserver Setup       "
echo "========================================="

# 1. Check if netcat / python3 is installed
if ! command -v nc >/dev/null 2>&1 && ! command -v python3 >/dev/null 2>&1; then
    echo "[*] Updating package index..."
    apt-get update -qq
    echo "[*] Installing netcat-openbsd and python3..."
    apt-get install -y -qq netcat-openbsd python3
fi

# 2. Get VM's internal IP address
VM_IP=$(ip -4 addr show eth0 2>/dev/null | grep -oP '(?<=inet\s)\d+(\.\d+){3}' || echo "127.0.0.1")
HOSTNAME=$(hostname)

echo "[+] VM Hostname : ${HOSTNAME}"
echo "[+] Internal IP : ${VM_IP}"
echo "[+] Target Port : ${PORT}"

# 3. Create a clean, visually informative HTML landing page
mkdir -p /tmp/cryo_web
cat << 'HTML_EOF' > /tmp/cryo_web/index.html
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>CryoSpawn MicroVM</title>
    <style>
        body {
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
            background: linear-gradient(135deg, #0f172a 0%, #1e1b4b 100%);
            color: #f8fafc;
            display: flex;
            justify-content: center;
            align-items: center;
            min-height: 100vh;
            margin: 0;
        }
        .card {
            background: rgba(30, 41, 59, 0.7);
            backdrop-filter: blur(12px);
            border: 1px solid rgba(255, 255, 255, 0.1);
            padding: 2.5rem;
            border-radius: 1rem;
            box-shadow: 0 20px 25px -5px rgba(0, 0, 0, 0.5), 0 8px 10px -6px rgba(0, 0, 0, 0.5);
            max-width: 480px;
            width: 100%;
            text-align: center;
        }
        .badge {
            display: inline-flex;
            align-items: center;
            background: #064e3b;
            color: #34d399;
            padding: 0.25rem 0.75rem;
            border-radius: 9999px;
            font-size: 0.75rem;
            font-weight: 600;
            text-transform: uppercase;
            letter-spacing: 0.05em;
            margin-bottom: 1rem;
            border: 1px solid #059669;
        }
        h1 { margin: 0 0 0.5rem 0; font-size: 1.75rem; font-weight: 700; color: #ffffff; }
        p { color: #94a3b8; font-size: 0.95rem; margin-top: 0; margin-bottom: 1.5rem; }
        .meta-box {
            background: #0f172a;
            border: 1px solid #334155;
            border-radius: 0.5rem;
            padding: 1rem;
            text-align: left;
            font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace;
            font-size: 0.85rem;
            line-height: 1.6;
        }
        .meta-row { display: flex; justify-content: space-between; }
        .label { color: #64748b; }
        .val { color: #38bdf8; font-weight: 600; }
        .footer {
            margin-top: 1.5rem;
            font-size: 0.75rem;
            color: #64748b;
        }
    </style>
</head>
<body>
    <div class="card">
        <div class="badge">● Online &amp; Auto-Woken</div>
        <h1>CryoSpawn Guest Node</h1>
        <p>This microVM was auto-resumed from snapshot by the Ghost Proxy.</p>
        <div class="meta-box">
            <div class="meta-row"><span class="label">Status:</span><span class="val" style="color: #4ade80;">Active</span></div>
            <div class="meta-row"><span class="label">Virtual Disk:</span><span class="val">Isolated CoW ext4</span></div>
            <div class="meta-row"><span class="label">Ingress:</span><span class="val">Nginx + UDS</span></div>
        </div>
        <div class="footer">Powered by Firecracker KVM MicroVM Engine</div>
    </div>
</body>
</html>
HTML_EOF

echo "[+] Landing page created at /tmp/cryo_web/index.html"

# 4. Handle Port 443 (HTTPS) vs Port 80 (HTTP)
if [ "$PORT" -eq 443 ]; then
    echo "[*] Setting up SSL self-signed certificate for Port 443..."
    if ! command -v openssl >/dev/null 2>&1; then
        apt-get install -y -qq openssl
    fi
    mkdir -p /tmp/cryo_web/ssl
    openssl req -x509 -newkey rsa:2048 -nodes -keyout /tmp/cryo_web/ssl/server.key -out /tmp/cryo_web/ssl/server.crt -days 365 -subj "/CN=${HOSTNAME}" >/dev/null 2>&1

    echo "[+] Launching Python3 HTTPS Webserver on port 443..."
    cat << 'PY_EOF' > /tmp/cryo_web/serve_https.py
import http.server, ssl, sys, os

os.chdir('/tmp/cryo_web')
server_address = ('0.0.0.0', 443)
httpd = http.server.HTTPServer(server_address, http.server.SimpleHTTPRequestHandler)

ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
ctx.load_cert_chain(certfile='/tmp/cryo_web/ssl/server.crt', keyfile='/tmp/cryo_web/ssl/server.key')
httpd.socket = ctx.wrap_socket(httpd.socket, server_side=True)

print("[✓] Serving HTTPS on 0.0.0.0:443 ... (Press Ctrl+C to stop)")
try:
    httpd.serve_forever()
except KeyboardInterrupt:
    print("\nShutting down web server.")
PY_EOF
    exec python3 /tmp/cryo_web/serve_https.py
else
    echo "[+] Launching Python3 HTTP Webserver on port ${PORT}..."
    cd /tmp/cryo_web
    echo "[✓] Serving HTTP on 0.0.0.0:${PORT} ... (Press Ctrl+C to stop)"
    exec python3 -m http.server "${PORT}"
fi
