/* Single source of truth for the map layout and every scenario.
   Each scenario step mirrors what the daemon code does (internal/*.hpp, main.cpp, nginx/server.sh). */
window.CS = window.CS || {};

CS.REPO = "https://github.com/NirjharDebnath/CryoSpawn/blob/main/";

/* Nodes on the live map (centre x/y in a 1200 x 760 viewBox). c = accent colour token. */
CS.NODES = {
  browser: { x: 95,  y: 100, label: "Browser",       sub: "http://vm0.cryo",  icon: "bi-globe2",          c: "--ice"  },
  ssh:     { x: 95,  y: 230, label: "SSH client",    sub: "nc -U socket",     icon: "bi-terminal",        c: "--ice"  },
  dash:    { x: 95,  y: 360, label: "Dashboard",     sub: "UI · curl",        icon: "bi-window-sidebar",  c: "--ice"  },
  nginx:   { x: 320, y: 100, label: "Nginx",         sub: "*.cryo :80/:443",  icon: "bi-signpost-split",  c: "--ice", lamp: true },
  uds:     { x: 480, y: 100, label: "Unix sockets",  sub: "/tmp/*.sock",      icon: "bi-plug",            c: "--ice", lamp: true },
  ghost:   { x: 640, y: 100, label: "Ghost Proxy",   sub: "proxy.hpp",        icon: "bi-shuffle",         c: "--wake", lamp: true },
  api:     { x: 320, y: 300, label: "REST API",      sub: ":8080 · main.cpp", icon: "bi-braces",          c: "--ice", lamp: true },
  mgr:     { x: 500, y: 300, w: 150, label: "VMManager", sub: "orchestrator.hpp", icon: "bi-cpu",         c: "--ice" },
  db:      { x: 320, y: 450, label: "SQLite",        sub: "cryospawn.db",     icon: "bi-database",        c: "--disk" },
  reaper:  { x: 500, y: 450, w: 165, label: "Reaper thread", sub: "waitpid() · 2 s", icon: "bi-recycle",  c: "--wake", lamp: true },
  nf:      { x: 660, y: 560, label: "Netfilter",     sub: "iptables · NAT",   icon: "bi-shield-lock",     c: "--net", lamp: true },
  tap0:    { x: 790, y: 230, w: 128, label: "cryo0", sub: "172.16.0.1/30",    icon: "bi-ethernet",        c: "--net"  },
  tap1:    { x: 790, y: 420, w: 128, label: "cryo1", sub: "172.16.0.5/30",    icon: "bi-ethernet",        c: "--net"  },
  vm0:     { x: 930, y: 230, w: 150, h: 120, vm: 0, label: "VM 0", icon: "bi-pc-display", c: "--run" },
  vm1:     { x: 930, y: 420, w: 150, h: 120, vm: 1, label: "VM 1", icon: "bi-pc-display", c: "--run" },
  tpl:     { x: 360, y: 670, label: "Golden images", sub: "rootfs · kernel", icon: "bi-box-seam",     c: "--disk" },
  vols:    { x: 580, y: 670, label: "volumes/",      sub: "vol_N.ext4",       icon: "bi-hdd-stack",       c: "--disk" },
  inst:    { x: 840, y: 670, w: 175, label: "instances/", sub: "disk · snapshots", icon: "bi-device-hdd",  c: "--disk" },
  inet:    { x: 1110, y: 560, label: "Internet",     sub: "default iface",    icon: "bi-cloud",           c: "--ice"  }
};

/* Order used for actor columns in the sequence diagrams. */
CS.ACTOR_ORDER = ["browser", "ssh", "dash", "nginx", "uds", "ghost", "api", "mgr", "reaper", "db",
  "nf", "inet", "tap0", "tap1", "vm0", "vm1", "tpl", "inst", "vols"];

/* Connectors. from/to give the direction of a forward packet. */
CS.EDGES = {
  "e-br-ng":    { from: "browser", to: "nginx", d: "M160,100 L255,100" },
  "e-ng-uds":   { from: "nginx", to: "uds",     d: "M385,100 L415,100" },
  "e-uds-gh":   { from: "uds", to: "ghost",     d: "M545,100 L575,100" },
  "e-ssh-uds":  { from: "ssh", to: "uds",       d: "M160,230 C330,230 470,200 470,128", label: "nc -U", lx: 300, ly: 222 },
  "e-gh-tap0":  { from: "ghost", to: "tap0",    d: "M705,100 C770,100 790,140 790,205", label: "TCP :80/:22/:443", lx: 770, ly: 92 },
  "e-gh-tap1":  { from: "ghost", to: "tap1",    d: "M690,128 C690,300 700,420 740,420" },
  "e-gh-mgr":   { from: "ghost", to: "mgr",     d: "M590,128 C590,200 530,220 530,272", label: "wake_fn", lx: 590, ly: 215 },
  "e-api-gh":   { from: "api", to: "ghost",     d: "M360,272 C360,185 620,200 625,128", label: "spawn listener", lx: 420, ly: 205 },
  "e-dash-api": { from: "dash", to: "api",      d: "M160,360 C215,360 210,300 255,300", label: "REST", lx: 196, ly: 318 },
  "e-api-mgr":  { from: "api", to: "mgr",       d: "M385,300 L430,300" },
  "e-mgr-db":   { from: "mgr", to: "db",        d: "M450,328 L370,425" },
  "e-mgr-reaper": { from: "mgr", to: "reaper",  d: "M500,328 L500,425" },
  "e-reaper-db":  { from: "reaper", to: "db",   d: "M430,450 L385,450" },
  "e-mgr-vm0":  { from: "mgr", to: "vm0",       d: "M570,305 C680,330 800,320 860,280", label: "fork · API socket", lx: 640, ly: 345 },
  "e-mgr-vm1":  { from: "mgr", to: "vm1",       d: "M570,315 C700,345 820,350 860,372" },
  "e-mgr-nf":   { from: "mgr", to: "nf",        d: "M570,322 C620,340 650,440 650,535" },
  "e-mgr-vols": { from: "mgr", to: "vols",      d: "M575,325 C582,330 582,340 582,360 L582,645" },
  "e-tap0-vm0": { from: "tap0", to: "vm0",      d: "M840,230 L860,230" },
  "e-tap1-vm1": { from: "tap1", to: "vm1",      d: "M840,420 L860,420" },
  "e-nf-tap0":  { from: "nf", to: "tap0",       d: "M720,535 L720,262 Q720,240 740,240" },
  "e-nf-tap1":  { from: "nf", to: "tap1",       d: "M730,545 C760,520 790,500 790,448" },
  "e-nf-inet":  { from: "nf", to: "inet",       d: "M725,560 L1045,560", label: "MASQUERADE", lx: 885, ly: 552 },
  "e-link":     { from: "vm0", to: "vm1",       d: "M930,288 L930,362" },
  "e-vm0-inst": { from: "vm0", to: "inst",      d: "M1000,250 L1012,250 L1012,670 L910,670" },
  "e-vm1-inst": { from: "vm1", to: "inst",      d: "M890,478 L890,642" },
  "e-vols-vm1": { from: "vols", to: "vm1",      d: "M645,660 C720,640 860,560 875,478", label: "extra drive", lx: 700, ly: 628 },
  "e-tpl-inst": { from: "tpl", to: "inst",      d: "M360,697 L360,728 L840,728 L840,697", label: "copy golden rootfs", lx: 600, ly: 721 }
};

/* Packet colour by edge, unless a step overrides it. */
CS.edgeColor = function (id) {
  if (/inst|vols|tpl|db/.test(id)) return "--disk";
  if (/nf|link|inet/.test(id)) return "--net";
  if (/gh-mgr/.test(id)) return "--wake";
  return "--ice";
};

/* World state shown on the map. */
CS.BASE_WORLD = {
  ready: { api: true, nf: true, reaper: true },
  vm:   ["absent", "absent"],
  tap:  [false, false],
  sock: [false, false],
  disk: [false, false],
  snap: [false, false],
  link: false,
  volExists: false,
  vol: -1
};

CS.world = function (patch) {
  const w = JSON.parse(JSON.stringify(CS.BASE_WORLD));
  return CS.patchWorld(w, patch || {});
};
CS.patchWorld = function (w, p) {
  for (const k in p) {
    const v = p[k];
    if (v && typeof v === "object" && !Array.isArray(v)) {
      for (const i in v) w[k][i] = v[i];
    } else {
      w[k] = v;
    }
  }
  return w;
};

/* One live VM with everything a running VM owns. */
const RUN0 = { vm: { 0: "running" }, tap: { 0: true }, sock: { 0: true }, disk: { 0: true } };
const RUN01 = { vm: { 0: "running", 1: "running" }, tap: { 0: true, 1: true }, sock: { 0: true, 1: true }, disk: { 0: true, 1: true } };

/* Step fields:
   t    text            ref  code reference
   p    packet groups (parallel); a group is an edge id or an array (chain).
        "-id" sends the packet backwards. {e, stop, deny} stops part-way (blocked).
   f    nodes to highlight during the step
   s    world patch applied when the step ends */
CS.SCENARIOS = [
  {
    id: "startup", group: "Lifecycle", title: "Daemon startup & recovery", icon: "bi-power", c: "--ice",
    trigger: "sudo ./cryospawn",
    summary: "Before any request is served, the daemon builds the firewall baseline, re-adopts VMs that are still alive, cleans up the ones that are not, and restarts their sockets.",
    setup: { ready: { api: false, nf: false, reaper: false }, vm: { 0: "running", 1: "dead" }, tap: { 0: true, 1: true }, disk: { 0: true, 1: true } },
    steps: [
      { t: "VMManager() starts and finds the default network interface with ip route show default.", ref: "network.hpp · get_default_interface()", f: ["mgr"] },
      { t: "Firewall baseline: ip_forward=1, MASQUERADE out of that interface, allow RELATED/ESTABLISHED, allow DNAT hairpin, and DROP every other cryo+ → cryo+ packet.", ref: "network.hpp · init_cryo_firewall_baseline()", p: ["e-mgr-nf"], s: { ready: { nf: true } } },
      { t: "Create the instances/ and volumes/ directories if they are missing.", ref: "orchestrator.hpp · VMManager()", p: ["e-mgr-vols"], f: ["inst", "vols"] },
      { t: "reconcile_state() reads every row of the vms table.", ref: "orchestrator.hpp · reconcile_state()", p: ["-e-mgr-db"] },
      { t: "VM 0: kill(pid, 0) succeeds, so the still-running Firecracker process is adopted back into memory.", ref: "orchestrator.hpp · reconcile_state()", p: ["e-mgr-vm0"], f: ["vm0"] },
      { t: "VM 1: its PID is gone, so the stale entry is cleaned up: TAP removed, disk and snapshot files deleted, socket files unlinked, DB row deleted.", ref: "orchestrator.hpp · cleanup_vm_resources()", p: [["e-mgr-nf", "e-nf-tap1"], "e-mgr-vm1", "e-mgr-db"], s: { vm: { 1: "absent" }, tap: { 1: false }, disk: { 1: false } } },
      { t: "start_reaper() launches a background thread that polls waitpid() every 2 seconds.", ref: "orchestrator.hpp · start_reaper()", p: ["e-mgr-reaper"], s: { ready: { reaper: true } } },
      { t: "main() starts a Ghost Proxy thread for each exposed port of every adopted VM.", ref: "main.cpp · main()", p: ["e-api-gh"], s: { sock: { 0: true } } },
      { t: "The HTTP server binds 0.0.0.0:8080 and serves the REST API and the dashboard files.", ref: "main.cpp · svr.listen()", f: ["api"], s: { ready: { api: true } } }
    ]
  },
  {
    id: "create", group: "Lifecycle", title: "Create a MicroVM", icon: "bi-plus-circle", c: "--run",
    trigger: "POST /api/vms",
    summary: "The request handler prepares the slot, network and process, replies right away, and a background thread then configures Firecracker and boots the guest.",
    setup: {},
    steps: [
      { t: "The dashboard sends POST /api/vms with vcpus, mem_mib, expose_ssh/http/https, project and rootfs_gb (default 3).", ref: "main.cpp · POST /api/vms", p: ["e-dash-api"] },
      { t: "create_vm() takes the lock and asks SQLite for the next free slot: slot 0.", ref: "database.hpp · get_next_free_slot()", p: [["e-api-mgr", "e-mgr-db", "-e-mgr-db"]] },
      { t: "Slot math: base = slot × 4 → host 172.16.0.1, guest 172.16.0.2, TAP cryo0, MAC AA:FC:AC:10:00:00, API socket /tmp/cryo_0.socket.", ref: "orchestrator.hpp · create_vm()", f: ["mgr"] },
      { t: "setup_cryo_network(): ip tuntap add cryo0, ip addr add 172.16.0.1/30, link up, FORWARD cryo0 → internet ACCEPT.", ref: "network.hpp · setup_cryo_network()", p: [["e-mgr-nf", "e-nf-tap0"]], s: { tap: { 0: true } } },
      { t: "fork() + execlp firecracker --api-sock /tmp/cryo_0.socket. The child calls setsid() and sends stdio to /dev/null.", ref: "orchestrator.hpp · create_vm()", p: ["e-mgr-vm0"], s: { vm: { 0: "booting" } } },
      { t: "db.insert_vm() records slot 0 with status booting.", ref: "database.hpp · insert_vm()", p: ["e-mgr-db"] },
      { t: "The handler starts one Ghost Proxy thread per exposed port and replies {id, ip, status: booting}.", ref: "main.cpp · POST /api/vms", p: ["e-api-gh", "-e-dash-api"], s: { sock: { 0: true } } },
      { t: "configure_and_start() runs on a detached thread: it waits for the API socket, then PUT /boot-source with the kernel and ip= boot args.", ref: "orchestrator.hpp · configure_and_start()", p: ["e-mgr-vm0"] },
      { t: "First boot only: copy the golden rootfs to instances/vm_0_rootfs.ext4. If rootfs_gb ≥ 3: truncate, e2fsck, resize2fs.", ref: "orchestrator.hpp · configure_and_start()", p: ["e-tpl-inst"], s: { disk: { 0: true } } },
      { t: "PUT /drives/rootfs points at the instance disk. PUT /drives/vol_N runs for every volume attached to this slot.", ref: "orchestrator.hpp · configure_and_start()", p: ["-e-vm0-inst"] },
      { t: "PUT /network-interfaces/eth0 binds the guest NIC (with its MAC) to TAP cryo0.", ref: "orchestrator.hpp · configure_and_start()", p: ["e-tap0-vm0"] },
      { t: "PUT /machine-config (vCPUs, RAM), then PUT /actions InstanceStart. Status becomes running in memory and SQLite.", ref: "orchestrator.hpp · configure_and_start()", p: ["e-mgr-vm0", "e-mgr-db"], s: { vm: { 0: "running" } } }
    ]
  },
  {
    id: "hibernate", group: "Lifecycle", title: "Hibernate (freeze to disk)", icon: "bi-snow", c: "--sleep",
    trigger: "POST /api/vms/0/hibernate",
    summary: "The VM is paused, its CPU state and RAM are written to two files, and the Firecracker process is killed. Network, disk and sockets stay in place.",
    setup: RUN0,
    steps: [
      { t: "The dashboard sends POST /api/vms/0/hibernate.", ref: "main.cpp · POST /api/vms/:id/hibernate", p: ["e-dash-api"] },
      { t: "hibernate_vm(0) takes the lock. Only a VM whose status is running can be frozen.", ref: "orchestrator.hpp · hibernate_vm()", p: ["e-api-mgr"] },
      { t: "PATCH /vm {state: Paused}: the guest's vCPUs stop.", ref: "orchestrator.hpp · hibernate_vm()", p: ["e-mgr-vm0"], s: { vm: { 0: "paused" } } },
      { t: "PUT /snapshot/create (Full): CPU and device state go to vm_0_state.snap, guest RAM to vm_0_mem.ram.", ref: "orchestrator.hpp · hibernate_vm()", p: ["e-vm0-inst"], s: { snap: { 0: true } } },
      { t: "Status is set to hibernated in memory and SQLite before the kill, so the Reaper keeps the files.", ref: "orchestrator.hpp · hibernate_vm()", p: ["e-mgr-db"] },
      { t: "kill(pid, SIGKILL): the Firecracker process is gone, so the VM uses no host CPU or RAM.", ref: "orchestrator.hpp · hibernate_vm()", p: ["e-mgr-vm0"], s: { vm: { 0: "hibernated" } } },
      { t: "Reaper: waitpid() reports the exit. Status is hibernated, so it only clears the PID. TAP, disk, snapshot and sockets stay.", ref: "orchestrator.hpp · start_reaper()", p: ["e-mgr-reaper"] },
      { t: "The API replies {status: hibernated}.", ref: "main.cpp", p: ["-e-dash-api"] }
    ]
  },
  {
    id: "wake", group: "Lifecycle", title: "Auto-wake on HTTP traffic", icon: "bi-lightning-charge", c: "--wake",
    trigger: "GET http://vm0.cryo",
    summary: "A request to a sleeping VM is caught by its Ghost Proxy, which restores the VM from the snapshot and then forwards the request.",
    setup: Object.assign({}, RUN0, { vm: { 0: "hibernated" }, snap: { 0: true } }),
    steps: [
      { t: "The browser resolves vm0.cryo to 127.0.0.1 (/etc/hosts) and sends GET to Nginx on port 80.", ref: "DESIGN.md · Nginx ingress", p: ["e-br-ng"] },
      { t: "server_name ~^vm(?<vmid>\\d+)\\.cryo$ captures 0 and proxy_pass sends it to unix:/tmp/cryo_vm_0_80.sock.", ref: "nginx/server.sh", p: ["e-ng-uds"] },
      { t: "The Ghost Proxy accept()s the connection and starts a handle_proxy_client thread.", ref: "proxy.hpp · run_ghost_proxy()", p: ["e-uds-gh"] },
      { t: "status_fn(0) reports hibernated, so wake_fn(0) calls wake_vm(0).", ref: "proxy.hpp · handle_proxy_client()", p: ["e-gh-mgr"] },
      { t: "wake_vm marks the VM waking (blocking concurrent wakes) and forks a fresh Firecracker on /tmp/cryo_0.socket.", ref: "orchestrator.hpp · wake_vm()", p: ["e-mgr-db", "e-mgr-vm0"], s: { vm: { 0: "waking" } } },
      { t: "PUT /snapshot/load restores CPU state and memory from the snapshot files.", ref: "orchestrator.hpp · wake_vm()", p: ["-e-vm0-inst"] },
      { t: "PATCH /vm {state: Resumed}. Status becomes running.", ref: "orchestrator.hpp · wake_vm()", p: ["e-mgr-vm0", "e-mgr-db"], s: { vm: { 0: "running" } } },
      { t: "The Ghost Proxy waits 600 ms, then connect()s to 172.16.0.2:80, retrying every 200 ms up to 50 times.", ref: "proxy.hpp · handle_proxy_client()", p: [["e-gh-tap0", "e-tap0-vm0"]] },
      { t: "Two relay_data threads pipe bytes both ways. The response travels back along the same path.", ref: "proxy.hpp · relay_data()", p: [["-e-tap0-vm0", "-e-gh-tap0", "-e-uds-gh", "-e-ng-uds", "-e-br-ng"]] }
    ]
  },
  {
    id: "restart", group: "Lifecycle", title: "Restart endpoint", icon: "bi-arrow-repeat", c: "--wake",
    trigger: "POST /api/vms/0/restart",
    summary: "reboot_vm() replaces the Firecracker process but keeps the TAP and disk. The handler then also creates a second VM.",
    note: "As written, this handler also calls create_vm(1, 512, …), so a new VM appears in the next free slot. See Known Issues #2.",
    setup: RUN0,
    steps: [
      { t: "The dashboard sends POST /api/vms/0/restart.", ref: "main.cpp · POST /api/vms/:id/restart", p: ["e-dash-api"] },
      { t: "reboot_vm(0): status restarting (tells the Reaper to keep resources), then SIGKILL.", ref: "orchestrator.hpp · reboot_vm()", p: [["e-api-mgr", "e-mgr-vm0"]], s: { vm: { 0: "restarting" } } },
      { t: "Reaper sees status restarting: keeps TAP and disk, only clears the PID.", ref: "orchestrator.hpp · start_reaper()", p: ["e-mgr-reaper"] },
      { t: "Unlink the old API socket and fork a new Firecracker on it. Status booting.", ref: "orchestrator.hpp · reboot_vm()", p: ["e-mgr-vm0", "e-mgr-db"], s: { vm: { 0: "booting" } } },
      { t: "configure_and_start(): the instance disk already exists, so there is no copy. Drives, NIC, machine config, InstanceStart → running.", ref: "orchestrator.hpp · configure_and_start()", p: ["-e-vm0-inst", "e-tap0-vm0"], s: { vm: { 0: "running" } } },
      { t: "The handler then calls create_vm(1, 512, no ports, \"default\", 1): slot 1 is allocated and booted.", ref: "main.cpp · POST /api/vms/:id/restart", p: [["e-mgr-nf", "e-nf-tap1"], "e-mgr-vm1"], s: { vm: { 1: "booting" }, tap: { 1: true } } },
      { t: "configure_and_start() for slot 1 copies the golden rootfs and boots it.", ref: "orchestrator.hpp · configure_and_start()", p: [["e-tpl-inst", "-e-vm1-inst"]], s: { vm: { 1: "running" }, disk: { 1: true } } }
    ]
  },
  {
    id: "destroy", group: "Lifecycle", title: "Destroy a VM", icon: "bi-trash3", c: "--deny",
    trigger: "DELETE /api/vms/0",
    summary: "The process is killed and the Reaper removes everything the VM owned: links, TAP, files, sockets and its database row.",
    note: "A hibernated VM has no process to kill, so terminate_vm() calls cleanup_vm_resources() directly.",
    setup: Object.assign({}, RUN01, { link: true, snap: { 0: true } }),
    steps: [
      { t: "The dashboard sends DELETE /api/vms/0.", ref: "main.cpp · DELETE /api/vms/:id", p: ["e-dash-api"] },
      { t: "terminate_vm(0): the Firecracker PID is alive, so it gets SIGKILL.", ref: "orchestrator.hpp · terminate_vm()", p: [["e-api-mgr", "e-mgr-vm0"]], s: { vm: { 0: "dead" } } },
      { t: "Reaper: waitpid() sees the exit. Status is not hibernated or restarting, so it runs cleanup_vm_resources().", ref: "orchestrator.hpp · start_reaper()", p: ["e-mgr-reaper"] },
      { t: "For each link: unlink_taps(cryo0, cryo1) deletes the FORWARD and MASQUERADE rules.", ref: "network.hpp · unlink_taps()", p: ["e-mgr-nf"], s: { link: false } },
      { t: "teardown_cryo_network(): ip link set cryo0 down, ip tuntap del cryo0.", ref: "network.hpp · teardown_cryo_network()", p: [["e-mgr-nf", "e-nf-tap0"]], s: { tap: { 0: false } } },
      { t: "Delete /tmp/cryo_0.socket, vm_0_rootfs.ext4, vm_0_state.snap and vm_0_mem.ram.", ref: "orchestrator.hpp · cleanup_vm_resources()", p: ["e-vm0-inst"], s: { disk: { 0: false }, snap: { 0: false } } },
      { t: "Unlink /tmp/cryo_vm_0_22.sock, _80.sock and _443.sock.", ref: "orchestrator.hpp · cleanup_vm_resources()", f: ["uds"], s: { sock: { 0: false } } },
      { t: "db.remove_vm(0) deletes the row. The HTTP handler also calls it and replies {status: deleted}.", ref: "database.hpp · remove_vm()", p: ["e-reaper-db", "-e-dash-api"], s: { vm: { 0: "absent" } } }
    ]
  },
  {
    id: "crash", group: "Lifecycle", title: "Crash cleanup by the Reaper", icon: "bi-exclamation-octagon", c: "--deny",
    trigger: "Firecracker exits on its own",
    summary: "Nobody has to call the API: the Reaper notices a dead process and frees its resources.",
    setup: RUN01,
    steps: [
      { t: "The Firecracker process for VM 1 exits unexpectedly.", ref: "", f: ["vm1"], s: { vm: { 1: "dead" } } },
      { t: "Reaper: waitpid(-1, WNOHANG) returns that PID. Status is running, so the VM is treated as dead.", ref: "orchestrator.hpp · start_reaper()", p: ["e-mgr-reaper"] },
      { t: "Adopted VMs (not children of this daemon) are checked with kill(pid, 0) in the same 2-second loop.", ref: "orchestrator.hpp · start_reaper()", f: ["reaper"] },
      { t: "cleanup_vm_resources(): remove links, delete TAP cryo1, delete disk and snapshot files and sockets, delete the DB row.", ref: "orchestrator.hpp · cleanup_vm_resources()", p: [["e-mgr-nf", "e-nf-tap1"], "e-vm1-inst", "e-reaper-db"], s: { vm: { 1: "absent" }, tap: { 1: false }, disk: { 1: false }, sock: { 1: false } } }
    ]
  },
  {
    id: "ssh", group: "Access", title: "SSH through a Unix socket", icon: "bi-terminal", c: "--ice",
    trigger: "ssh -o ProxyCommand=\"nc -U /tmp/cryo_vm_0_22.sock\" root@localhost",
    summary: "SSH reaches the guest without any host TCP port: netcat talks to the socket and the Ghost Proxy relays to port 22.",
    note: "If the VM were hibernated, the Ghost Proxy would wake it first, exactly as in the HTTP auto-wake flow.",
    setup: RUN0,
    steps: [
      { t: "ssh runs nc -U, which connects to the VM's port-22 Unix socket. No host TCP port is opened.", ref: "DESIGN.md · SSH via ProxyCommand", p: ["e-ssh-uds"] },
      { t: "The Ghost Proxy accept()s. status_fn(0) is false (the VM is running), so no wake is needed.", ref: "proxy.hpp · handle_proxy_client()", p: ["e-uds-gh"] },
      { t: "connect() to 172.16.0.2:22 through TAP cryo0.", ref: "proxy.hpp · handle_proxy_client()", p: [["e-gh-tap0", "e-tap0-vm0"]] },
      { t: "relay_data threads pipe the SSH session both ways until either side closes.", ref: "proxy.hpp · relay_data()", p: [["-e-tap0-vm0", "-e-gh-tap0", "-e-uds-gh", "-e-ssh-uds"]] }
    ]
  },
  {
    id: "ports", group: "Access", title: "Expose or hide ports", icon: "bi-toggles", c: "--ice",
    trigger: "PATCH /api/vms/0/ports",
    summary: "Turning a port on starts a socket listener; turning it off closes it and removes the socket file.",
    setup: Object.assign({}, RUN0, { sock: { 0: false } }),
    steps: [
      { t: "The dashboard sends PATCH /api/vms/0/ports with ssh_exposed, http_exposed and https_exposed.", ref: "main.cpp · PATCH /api/vms/:id/ports", p: ["e-dash-api"] },
      { t: "update_vm_ports() updates memory and SQLite.", ref: "orchestrator.hpp · update_vm_ports()", p: [["e-api-mgr", "e-mgr-db"]] },
      { t: "Each port turned on without a listener gets run_ghost_proxy(): bind /tmp/cryo_vm_0_<port>.sock, chmod 0666 so Nginx can use it.", ref: "proxy.hpp · run_ghost_proxy()", p: ["e-api-gh"], s: { sock: { 0: true } } },
      { t: "Each port turned off runs stop_ghost_proxy(): shutdown and close the listening fd, then unlink the socket file.", ref: "proxy.hpp · stop_ghost_proxy()", f: ["uds", "ghost"] },
      { t: "The API replies {status: ports updated}.", ref: "main.cpp", p: ["-e-dash-api"] }
    ]
  },
  {
    id: "poll", group: "Access", title: "Dashboard refresh", icon: "bi-arrow-clockwise", c: "--ice",
    trigger: "every 2 s (app.js)",
    summary: "The dashboard keeps itself current by polling three read endpoints.",
    setup: RUN01,
    steps: [
      { t: "app.js fetches /api/vms, /api/links and /api/volumes in parallel.", ref: "internal/ui/app.js · fetchData()", p: ["e-dash-api"] },
      { t: "/api/vms lists the VMs held in VMManager memory.", ref: "orchestrator.hpp · list_vms()", p: ["e-api-mgr"] },
      { t: "/api/links and /api/volumes are read from SQLite.", ref: "database.hpp", p: [["e-api-mgr", "e-mgr-db", "-e-mgr-db"]] },
      { t: "JSON comes back and the cards, links and volume list are redrawn.", ref: "internal/ui/app.js", p: ["-e-dash-api"] }
    ]
  },
  {
    id: "internet", group: "Network", title: "Outbound internet (NAT)", icon: "bi-globe", c: "--net",
    trigger: "VM 0 → 8.8.8.8",
    summary: "Each VM reaches the internet through its own TAP, kernel forwarding and NAT masquerade.",
    setup: RUN0,
    steps: [
      { t: "The guest sends a packet to its default gateway 172.16.0.1 (set by the ip= kernel boot argument).", ref: "orchestrator.hpp · configure_and_start()", p: ["-e-tap0-vm0"] },
      { t: "ip_forward=1 and FORWARD -i cryo0 -o <iface> ACCEPT let it leave the TAP.", ref: "network.hpp · setup_cryo_network()", p: ["-e-nf-tap0"] },
      { t: "POSTROUTING MASQUERADE rewrites the source to the host's address.", ref: "network.hpp · init_cryo_firewall_baseline()", p: ["e-nf-inet"] },
      { t: "Replies match RELATED,ESTABLISHED, are translated back, and are forwarded to 172.16.0.2.", ref: "network.hpp", p: [["-e-nf-inet", "e-nf-tap0", "e-tap0-vm0"]] }
    ]
  },
  {
    id: "link", group: "Network", title: "Isolation, then linking", icon: "bi-link-45deg", c: "--run",
    trigger: "POST /api/links {vm1: 0, vm2: 1}",
    summary: "VMs cannot see each other by default. A link inserts four rules that open a path between two VMs of the same project.",
    setup: RUN01,
    steps: [
      { t: "VM 0 tries to reach VM 1. The baseline rule FORWARD -i cryo+ -o cryo+ -d 172.16.0.0/16 -j DROP blocks it.", ref: "network.hpp · init_cryo_firewall_baseline()", p: [{ e: "e-link", stop: 0.5, deny: true }] },
      { t: "The dashboard sends POST /api/links.", ref: "main.cpp · POST /api/links", p: ["e-dash-api"] },
      { t: "link_microvms(): both VMs must exist and share a project; otherwise the API answers 400 \"Cross-project linking is denied\".", ref: "orchestrator.hpp · link_microvms()", p: ["e-api-mgr"] },
      { t: "db.add_link() stores the ordered pair (INSERT OR IGNORE).", ref: "database.hpp · add_link()", p: ["e-mgr-db"] },
      { t: "link_taps(): FORWARD cryo0 ⇄ cryo1 ACCEPT and nat POSTROUTING MASQUERADE -o cryo0 / -o cryo1, all inserted at position 1.", ref: "network.hpp · link_taps()", p: ["e-mgr-nf"], s: { link: true } },
      { t: "VM 0 and VM 1 can now talk in both directions.", ref: "", p: [["e-link", "-e-link"]] }
    ]
  },
  {
    id: "unlink", group: "Network", title: "Unlink VMs", icon: "bi-scissors", c: "--deny",
    trigger: "DELETE /api/links/0/1",
    summary: "Removing the link deletes the same four rules, and the baseline DROP applies again.",
    setup: Object.assign({}, RUN01, { link: true }),
    steps: [
      { t: "The dashboard sends DELETE /api/links/0/1.", ref: "main.cpp · DELETE /api/links/:a/:b", p: ["e-dash-api"] },
      { t: "unlink_microvms() removes the row from network_links.", ref: "database.hpp · remove_link()", p: [["e-api-mgr", "e-mgr-db"]] },
      { t: "unlink_taps(): iptables -D removes both FORWARD and both MASQUERADE rules.", ref: "network.hpp · unlink_taps()", p: ["e-mgr-nf"], s: { link: false } },
      { t: "Traffic between the two VMs is dropped again.", ref: "", p: [{ e: "e-link", stop: 0.5, deny: true }] }
    ]
  },
  {
    id: "volume", group: "Storage", title: "Create & attach a volume", icon: "bi-hdd-stack", c: "--disk",
    trigger: "POST /api/volumes → POST /api/volumes/1/attach",
    summary: "A volume is a plain ext4 file. Attaching it to a running VM reboots the VM so Firecracker can add the drive.",
    note: "Detach is the same in reverse (attached_vm_id = -1, reboot if running). Deleting a volume unlinks its file and its row. Attach/detach only reboot VMs whose status is running.",
    setup: RUN01,
    steps: [
      { t: "The dashboard sends POST /api/volumes {name, size_gb}.", ref: "main.cpp · POST /api/volumes", p: ["e-dash-api"] },
      { t: "create_volume(): db.insert_volume() returns the new id (1).", ref: "orchestrator.hpp · create_volume()", p: [["e-api-mgr", "e-mgr-db"]] },
      { t: "truncate -s <size>G volumes/vol_1.ext4, then mkfs.ext4.", ref: "orchestrator.hpp · create_volume()", p: ["e-mgr-vols"], s: { volExists: true } },
      { t: "POST /api/volumes/1/attach {vm_id: 1} sets attached_vm_id in SQLite.", ref: "main.cpp · POST /api/volumes/:id/attach", p: [["e-dash-api", "e-api-mgr", "e-mgr-db"]], s: { vol: 1 } },
      { t: "VM 1 is running, so reboot_vm(1): status restarting, SIGKILL.", ref: "orchestrator.hpp · reboot_vm()", p: ["e-mgr-vm1"], s: { vm: { 1: "restarting" } } },
      { t: "Reaper sees restarting: keeps TAP and disk, only clears the PID.", ref: "orchestrator.hpp · start_reaper()", p: ["e-mgr-reaper"] },
      { t: "Fork a new Firecracker on the same API socket. Status booting.", ref: "orchestrator.hpp · reboot_vm()", p: ["e-mgr-vm1"], s: { vm: { 1: "booting" } } },
      { t: "configure_and_start(): the instance disk already exists (no copy). PUT /drives/rootfs.", ref: "orchestrator.hpp · configure_and_start()", p: ["-e-vm1-inst"] },
      { t: "PUT /drives/vol_1 adds volumes/vol_1.ext4 as an extra drive.", ref: "orchestrator.hpp · configure_and_start()", p: ["e-vols-vm1"] },
      { t: "Network interface, machine config, InstanceStart → running with the volume attached.", ref: "orchestrator.hpp · configure_and_start()", p: ["e-tap1-vm1", "e-mgr-vm1"], s: { vm: { 1: "running" } } }
    ]
  }
];

CS.TOUR = ["startup", "create", "internet", "hibernate", "wake", "ssh", "link", "volume", "destroy"];

CS.byId = function (id) { return CS.SCENARIOS.find(s => s.id === id); };
