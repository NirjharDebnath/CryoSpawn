# 🐞 Known Issues

> **Written for:** developers and operators who run or contribute to CryoSpawn and want to know its current limitations.

These were found by reading the source. None of them has been reproduced by running the daemon.

## 📋 Summary

| # | Issue | Impact | Where |
| :-: | :--- | :--- | :--- |
| 1 | Hard-coded absolute paths | Daemon only works from one developer's checkout | `main.cpp`, `orchestrator.hpp` |
| 2 | `restart` creates a second VM | An unexpected extra VM appears after a restart | `main.cpp` |
| 3 | Kernel and root disk images not in the repo | A fresh clone cannot boot a VM | repo-wide |
| 4 | API has no authentication | Anyone who can reach port 8080 controls all VMs | `main.cpp` |
| 5 | `host_port` is ignored by `/proxy` | The request field has no effect | `main.cpp` |
| 6 | Root disk default differs between API and schema | Confusing `rootfs_gb` values | `main.cpp`, `database.hpp` |

## 🔧 Details

### 1. Hard-coded absolute paths

The kernel, template root disk, UI mount point, and the `instances/` and `volumes/` directories are all absolute paths under `/home/nirjhar/Python Codes/Einstein/CryoSpawn`.

- **Workaround:** edit the paths listed in [SETUP.md](SETUP.md#️-configure-paths) before building.
- **Suggested fix:** resolve paths relative to the working directory or take them from environment variables or arguments.

### 2. `POST /api/vms/:id/restart` creates a second VM

The handler calls `reboot_vm(id, ...)` and then `create_vm(1, 512, false, false, false, "default", 1)`. The reboot restarts the requested VM correctly. The extra `create_vm` call then allocates a new slot with default settings and starts it.

- **Workaround:** if you only want a restart, delete the extra VM afterwards.
- **Suggested fix:** remove the `create_vm` call, or document the endpoint as "restart and add a VM" if that is intended.

### 3. Kernel and root disk images are not provided

`vmlinux/ubuntu-vmlinux.bin` and `rootfs/ubuntu-rootfs.ext4` are not in the repository, and no doc or script explains how to produce them.

- **Suggested fix:** add a script or a doc describing how to build or download compatible images.

### 4. No authentication on the API

The daemon listens on `0.0.0.0:8080`, allows every origin through CORS (`Access-Control-Allow-Origin: *`), and has no login. It runs as root and manages firewall rules.

> [!WARNING]
> Do not run it on an untrusted network. Block port 8080 from outside the machine, or bind to localhost.

### 5. `host_port` is ignored by `POST /api/vms/:id/proxy`

The body requires `host_port`, but only `vm_port` is used. The proxy always listens on `/tmp/cryo_vm_<id>_<vm_port>.sock`. `host_port` is also a legacy column in the database.

### 6. Root disk size defaults differ

The API defaults `rootfs_gb` to `3`, while the database column defaults to `1`. The disk is only expanded when `rootfs_gb` is `3` or more, so values of `1` or `2` keep the template image size.

## ❓ Needs verification

- **Volumes and hibernated VMs:** attaching or detaching a volume only reboots a VM whose status is `running`. Whether a hibernated VM picks up the change when it is woken from its snapshot has not been checked.
