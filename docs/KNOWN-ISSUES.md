# Known Issues and Operational Limits

This list describes the current implementation rather than the roadmap.

| Severity | Issue | Status | Guidance |
| :--- | :--- | :--- | :--- |
| High | Daemon port 9090 is trusted by identity headers | Open | Bind remains loopback-only. Set `CRYOSPAWN_INTERNAL_SECRET` on Flask and the daemon; never publish port 9090 |
| High | Default admin credentials | Open | Change `admin123` immediately after first login |
| High | Development server and unbounded network binding | Open | Flask's built-in server listens on `0.0.0.0:8080`; use a hardened reverse proxy for exposure |
| Medium | Session key can rotate when the process restarts | Open | Set a stable `CRYOSPAWN_SECRET_KEY`; this invalidates existing sessions after restart if omitted |
| Medium | Public DNS/Nginx files are examples, not a complete deployment | Open | Use the local Nginx instructions and verify socket names before exposing a domain |
| Medium | `nginx/server.sh` and `utils/loopback.sh` use legacy names | Open | The current daemon uses `cryo-<hex>` and `/tmp/cryo-<hex>_<port>.sock`; update example configuration before use |
| Medium | Test runner assumes Python 3.12 in `.venv` | Open | Create that environment or adjust `tests/run_tests.sh` for the installed Python |
| Low | Guest assets are downloaded by script | External dependency | Run `utils/download_assets.sh`; the kernel and rootfs are not stored in Git |
| Low | Snapshot support depends on Firecracker and guest/KVM compatibility | Open | Use matching x86/aarch64 assets and verify `/dev/kvm` |
| Low | Full topology IaC, branching, cryptographic mesh, and admin telemetry | Planned | These features are roadmap items, not implemented services |

## Security boundaries

The Flask proxy is the supported external entry point. It validates login sessions, injects `X-User-ID` and `X-Role`, and forwards API requests to the loopback daemon.

The daemon also enforces ownership checks for individual resources and validates an internal shared secret when `CRYOSPAWN_INTERNAL_SECRET` is configured. The shared secret is defense against local misuse only; it is not a replacement for process isolation or a production authentication system.

Do not expose port 9090. Do not rely on CORS alone for authorization.

## Resource and lifecycle caveats

- VM resource limits are enforced by the API, but host disk and memory exhaustion are not subject to hard tenant quotas.
- `?all=true` allows an admin to view or modify across owners. Its use on write routes should be deliberate.
- Hibernating a VM requires snapshot support and can fail if Firecracker does not have enough permissions or compatible device state.
- Volume attach/detach updates snapshot state and reboots or invalidates as appropriate. Treat it as a lifecycle operation, not a live hotplug guarantee.
- Links can only be created for VMs with compatible ownership and routing policies.
- There is no audited action log, tenant quota manager, or host-wide emergency control yet.

## Fixed in the current implementation

- VM, volume, and project creation now records the caller as owner.
- VM and volume routes perform ownership checks.
- Project names and resource names are validated.
- SQLite writes use WAL mode and busy timeouts.
- Firecracker proxy and TCP relay code uses positive-PID checks and close-safe relaying.
- Snapshot and stopped states are preserved across daemon restarts.
- Slot allocation reuses freed gaps.
- The asset downloader creates a compatible kernel/rootfs for supported architectures.

See [SETUP.md](SETUP.md) for hardening and operational details.
