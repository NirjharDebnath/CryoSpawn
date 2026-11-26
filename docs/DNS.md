# DNS and Ingress

## Current local ingress

The implementation creates Ghost Proxy Unix sockets under `/tmp`:

```text
/tmp/cryo-<hex>_22.sock
/tmp/cryo-<hex>_80.sock
/tmp/cryo-<hex>_443.sock
```

The host name convention is `cryo-<hex>.cryo`. Add a host entry for HTTP testing:

```bash
echo '127.0.0.1 cryo-da05a06c.cryo' | sudo tee -a /etc/hosts
```

A matching Nginx server block can proxy to the VM's Unix socket:

```nginx
server {
    listen 80;
    server_name ~^cryo-(?<hex>[a-f0-9]+)\.cryo$;

    location / {
        proxy_pass http://unix:/tmp/cryo-$hex_80.sock:;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
    }
}
```

> [!WARNING]
> The socket name in the example is implementation-sensitive. The repository's `nginx/server.sh` still contains a legacy `vmN.cryo` pattern and must not be used unchanged. Verify the VM UUID and socket before starting Nginx.

## SSH

```bash
ssh -o ProxyCommand="nc -U /tmp/cryo-da05a06c_22.sock" root@localhost
```

The Ghost Proxy wakes a hibernated VM when traffic arrives.

## Public deployment target

A future public deployment can use Cloudflare Tunnel and Nginx:

```text
Internet
  -> Cloudflare edge
  -> cloudflared
  -> Nginx
       -> app.example.com to Flask :8080
       -> <hex>.example.com to /tmp/cryo-<hex>_80.sock
```

Before exposing a dashboard publicly:

1. Set a stable `CRYOSPAWN_SECRET_KEY`.
2. Set `CRYOSPAWN_INTERNAL_SECRET` on Flask and the daemon.
3. Change the default admin password.
4. Keep port 9090 loopback-only.
5. Place the Flask development server behind an authenticated, maintained reverse proxy or production WSGI server.
6. Ensure wildcard DNS, TLS, and host-header routing match the actual VM UUID.

The browser terminal, public admin subdomain, dynamic Cloudflare API integration, and topology console are planned features. They are not implemented by the current repository.
