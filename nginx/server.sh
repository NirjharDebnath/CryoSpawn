sudo bash -c 'cat << "EOF" > /etc/nginx/conf.d/cryospawn.conf
# 1. Platform Ingress (Python Flask Dashboard & Auth API)
server {
    listen 80;
    server_name cryospawn.* app.* admin.*;

    location / {
        proxy_pass http://127.0.0.1:8080;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";
        proxy_read_timeout 3600s;
        proxy_send_timeout 3600s;
    }
}

# 2. MicroVM Dynamic Ingress (HTTP Port 80)
server {
    listen 80;
    server_name "~^(?:cryo-)?(?<vmhex>[a-f0-9]{8})\.(?<domain>.+)$" ~^vm(?<vmid>\d+)\.cryo$;

    location / {
        set $sock "/tmp/cryo-${vmhex}_80.sock";
        if ($vmhex = "") {
            set $sock "/tmp/cryo_vm_${vmid}_80.sock";
        }

        proxy_pass http://unix:$sock;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";
        proxy_read_timeout 3600s;
    }
}
EOF'

# Ensure systemd nginx service can read /tmp/cryo-*.sock (disable PrivateTmp)
if command -v systemctl >/dev/null 2>&1 && systemctl list-unit-files nginx.service >/dev/null 2>&1; then
    sudo mkdir -p /etc/systemd/system/nginx.service.d
    printf "[Service]\nPrivateTmp=false\n" | sudo tee /etc/systemd/system/nginx.service.d/override.conf >/dev/null
    sudo systemctl daemon-reload || true
fi

# Reload Nginx to apply the new config instantly if running
if command -v nginx >/dev/null 2>&1; then
    sudo nginx -t && (sudo systemctl restart nginx 2>/dev/null || sudo nginx -s reload || true)
fi
