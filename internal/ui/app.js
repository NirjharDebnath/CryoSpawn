const API_URL = 'http://localhost:8080/api/vms';
const LINKS_API_URL = 'http://localhost:8080/api/links';
const VOLUMES_API_URL = 'http://localhost:8080/api/volumes';

// DOM Elements
const vmTableBody = document.getElementById('vm-table-body');
const emptyState = document.getElementById('empty-state');
const spawnForm = document.getElementById('spawn-form');
const spawnBtn = document.getElementById('spawn-btn');
const connStatus = document.getElementById('connection-status');
const vmCountBadge = document.getElementById('vm-count-badge');

const linkForm = document.getElementById('link-form');
const linkVm1 = document.getElementById('link-vm1');
const linkVm2 = document.getElementById('link-vm2');
const linksContainer = document.getElementById('links-container');

const volumeForm = document.getElementById('volume-form');
const volumesContainer = document.getElementById('volumes-container');

let cachedVMs = [];
let cachedVolumes = [];

// Fetch VMs, Links, and Volumes concurrently
async function fetchData() {
    try {
        const [vmsRes, linksRes, volsRes] = await Promise.all([
            fetch(API_URL),
            fetch(LINKS_API_URL),
            fetch(VOLUMES_API_URL)
        ]);

        if (!vmsRes.ok || !linksRes.ok || !volsRes.ok) throw new Error('Daemon communication error');

        const vms = await vmsRes.json();
        const links = await linksRes.json();
        const volumes = await volsRes.json();
        cachedVMs = vms;
        cachedVolumes = volumes;

        connStatus.innerHTML = `<span class="h-2 w-2 rounded-full bg-green-500 mr-2"></span> Connected`;
        connStatus.className = "flex items-center text-sm font-medium text-green-600";

        renderTable(vms);
        updateLinkSelectors(vms);
        renderLinks(links, vms);
        renderVolumes(volumes, vms);
    } catch (error) {
        connStatus.innerHTML = `<span class="h-2 w-2 rounded-full bg-red-500 mr-2"></span> Offline`;
        connStatus.className = "flex items-center text-sm font-medium text-red-600";
    }
}

// Render the VM table
function renderTable(vms) {
    vmCountBadge.textContent = `${vms.length} VM${vms.length === 1 ? '' : 's'}`;

    if (vms.length === 0) {
        vmTableBody.innerHTML = '';
        emptyState.classList.remove('hidden');
        return;
    }

    emptyState.classList.add('hidden');
    vmTableBody.innerHTML = vms.map(vm => `
        <tr class="hover:bg-slate-50 transition-colors">
            <td class="px-6 py-4 whitespace-nowrap">
                <div class="font-medium text-slate-900 flex items-center space-x-2">
                    <span>VM ${vm.id}</span>
                    <span class="text-xs bg-slate-100 text-slate-600 px-2 py-0.5 rounded font-mono">${vm.project || 'default'}</span>
                </div>
                <div class="text-xs text-slate-400 font-mono mt-0.5">${vm.tap_name}</div>
            </td>
            <td class="px-6 py-4 whitespace-nowrap">
                ${(() => {
                    if (vm.status === 'running') 
                        return `<span class="inline-flex items-center px-2.5 py-0.5 rounded-full text-xs font-medium bg-green-100 text-green-800 border border-green-200">Running</span>`;
                    if (vm.status === 'hibernated') 
                        return `<span class="inline-flex items-center px-2.5 py-0.5 rounded-full text-xs font-medium bg-purple-100 text-purple-800 border border-purple-200">Hibernated ❄️</span>`;
                    if (vm.status === 'booting') 
                        return `<span class="inline-flex items-center px-2.5 py-0.5 rounded-full text-xs font-medium bg-blue-100 text-blue-800 border border-blue-200 status-booting">Booting...</span>`;
                    return `<span class="inline-flex items-center px-2.5 py-0.5 rounded-full text-xs font-medium bg-gray-100 text-gray-800 border border-gray-200">${vm.status}</span>`;
                })()}
            </td>
            <td class="px-6 py-4 whitespace-nowrap">
                <div class="flex items-center space-x-2">
                    <span class="text-xs font-semibold uppercase tracking-wider text-slate-400">VM:</span>
                    <span class="text-sm font-mono text-blue-800 font-medium select-all bg-slate-100 px-1 rounded">${vm.ip}</span>
                </div>
                <div class="flex items-center space-x-2 mt-0.5">
                    <span class="text-xs font-semibold uppercase tracking-wider text-slate-400">GW:</span>
                    <span class="text-xs font-mono text-slate-500">${vm.host_ip}</span>
                </div>
                ${vm.ssh_exposed ? `
                    <div class="mt-2 flex items-center space-x-2">
                        <span class="text-xs bg-slate-100 px-1.5 py-0.5 rounded font-mono text-slate-500 border border-slate-200">Port 22</span>
                        <button onclick='copySSH(this, ${vm.id})' class="text-xs bg-blue-50 text-blue-600 hover:bg-blue-100 px-2 py-1 rounded border border-blue-200 transition-colors flex items-center" title="Copy SSH Command">
                            <svg class="w-3 h-3 mr-1" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M8 16H6a2 2 0 01-2-2V6a2 2 0 012-2h8a2 2 0 012 2v2m-6 12h8a2 2 0 002-2v-8a2 2 0 00-2-2h-8a2 2 0 00-2 2v8a2 2 0 002 2z"></path></svg>
                            Copy SSH Command
                        </button>
                    </div>
                ` : ''}
                ${vm.http_exposed ? `
                    <div class="mt-1 text-xs bg-slate-100 p-1 rounded font-mono text-green-600 inline-block select-all" title="Nginx Ingress">
                        http://vm${vm.id}.cryo
                    </div><br>
                ` : ''}
                ${vm.https_exposed ? `
                    <div class="mt-1 text-xs bg-slate-100 p-1 rounded font-mono text-green-600 inline-block select-all" title="Nginx Ingress">
                        https://vm${vm.id}.cryo
                    </div>
                ` : ''}
                <div class="mt-2 flex items-center space-x-3 text-xs border-t border-slate-100 pt-2">
                    <label class="flex items-center space-x-1 cursor-pointer">
                        <input type="checkbox" onchange="updatePorts(${vm.id}, this.checked, ${vm.http_exposed}, ${vm.https_exposed})" ${vm.ssh_exposed ? 'checked' : ''} class="rounded border-slate-300 text-blue-600 focus:ring-blue-500 cursor-pointer">
                        <span class="text-slate-600 font-medium">SSH</span>
                    </label>
                    <label class="flex items-center space-x-1 cursor-pointer">
                        <input type="checkbox" onchange="updatePorts(${vm.id}, ${vm.ssh_exposed}, this.checked, ${vm.https_exposed})" ${vm.http_exposed ? 'checked' : ''} class="rounded border-slate-300 text-blue-600 focus:ring-blue-500 cursor-pointer">
                        <span class="text-slate-600 font-medium">HTTP</span>
                    </label>
                    <label class="flex items-center space-x-1 cursor-pointer">
                        <input type="checkbox" onchange="updatePorts(${vm.id}, ${vm.ssh_exposed}, ${vm.http_exposed}, this.checked)" ${vm.https_exposed ? 'checked' : ''} class="rounded border-slate-300 text-blue-600 focus:ring-blue-500 cursor-pointer">
                        <span class="text-slate-600 font-medium">HTTPS</span>
                    </label>
                </div>
            </td>
            <td class="px-6 py-4 whitespace-nowrap text-sm text-slate-600">
                ${vm.vcpus} vCPU • ${vm.mem_mib} MB • ${vm.rootfs_gb} GB Disk
            </td>
            <td class="px-6 py-4 whitespace-nowrap text-right space-x-2">
                ${vm.status === 'running' ? `
                    <button onclick="hibernateVM(${vm.id})" class="text-purple-600 hover:text-purple-800 hover:bg-purple-50 px-3 py-1 rounded transition-colors text-sm font-medium">
                        Hibernate
                    </button>
                ` : ''}
                ${vm.status === 'hibernated' ? `
                    <button onclick="wakeVM(${vm.id})" class="text-green-600 hover:text-green-800 hover:bg-green-50 px-3 py-1 rounded transition-colors text-sm font-medium">
                        Wake
                    </button>
                ` : ''}
                <button onclick="deleteVM(${vm.id})" class="text-red-500 hover:text-red-700 hover:bg-red-50 px-3 py-1 rounded transition-colors text-sm font-medium">
                    Destroy
                </button>
            </td>
        </tr>
    `).join('');
}

// Populate the VM select boxes for linking
function updateLinkSelectors(vms) {
    const runningVMs = vms.filter(v => v.status === 'running');
    const prev1 = linkVm1.value;
    const prev2 = linkVm2.value;

    const options = runningVMs.map(vm => 
        `<option value="${vm.id}">VM ${vm.id} (${vm.project || 'default'} - ${vm.ip})</option>`
    ).join('');

    linkVm1.innerHTML = options;
    linkVm2.innerHTML = options;

    if (prev1) linkVm1.value = prev1;
    if (prev2) linkVm2.value = prev2;
}

// Render active links
function renderLinks(links, vms) {
    if (!links || links.length === 0) {
        linksContainer.innerHTML = `<p class="text-xs text-slate-400 italic">No custom routes active. VMs are currently isolated.</p>`;
        return;
    }

    const vmMap = new Map(vms.map(v => [v.id, v]));

    linksContainer.innerHTML = links.map(link => {
        const v1 = vmMap.get(link.vm1);
        const v2 = vmMap.get(link.vm2);

        const label1 = v1 ? `VM ${v1.id} (${v1.ip})` : `VM ${link.vm1}`;
        const label2 = v2 ? `VM ${v2.id} (${v2.ip})` : `VM ${link.vm2}`;
        const proj = v1?.project || v2?.project || 'routed';

        return `
            <div class="flex items-center justify-between p-2.5 bg-slate-50 border border-slate-200 rounded-lg text-sm">
                <div class="flex items-center space-x-2">
                    <span class="text-xs bg-purple-100 text-purple-800 font-mono px-2 py-0.5 rounded font-semibold">${proj}</span>
                    <span class="font-mono text-slate-800">${label1}</span>
                    <span class="text-blue-500 font-bold">⟷</span>
                    <span class="font-mono text-slate-800">${label2}</span>
                </div>
                <button onclick="unlinkVMs(${link.vm1}, ${link.vm2})" class="text-xs text-red-600 hover:text-red-800 font-medium px-2 py-1 hover:bg-red-50 rounded">
                    Sever Link
                </button>
            </div>
        `;
    }).join('');
}

// Spawn Form Handler
spawnForm.addEventListener('submit', async (e) => {
    e.preventDefault();
    spawnBtn.disabled = true;
    spawnBtn.textContent = "Initializing...";

    const payload = {
        project: document.getElementById('project-name').value.trim() || 'default',
        vcpus: parseInt(document.getElementById('vcpus').value),
        mem_mib: parseInt(document.getElementById('ram').value),
        expose_ssh: document.getElementById('expose-ssh').checked,
        expose_http: document.getElementById('expose-http').checked,
        expose_https: document.getElementById('expose-https').checked,
        rootfs_gb: parseInt(document.getElementById('rootfs_gb').value)
    };

    try {
        const response = await fetch(API_URL, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(payload)
        });
        if (!response.ok) throw new Error("Server rejected request");
        fetchData();
    } catch (error) {
        alert("Failed to spawn VM: " + error.message);
    } finally {
        spawnBtn.disabled = false;
        spawnBtn.textContent = "Initialize VM";
    }
});

// Link Form Handler
linkForm.addEventListener('submit', async (e) => {
    e.preventDefault();
    const vm1 = parseInt(linkVm1.value);
    const vm2 = parseInt(linkVm2.value);

    if (isNaN(vm1) || isNaN(vm2) || vm1 === vm2) {
        alert("Please select two different VMs to bridge.");
        return;
    }

    try {
        const res = await fetch(LINKS_API_URL, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ vm1, vm2 })
        });
        if (!res.ok) {
            const err = await res.json();
            throw new Error(err.error || "Failed to link");
        }
        fetchData();
    } catch (err) {
        alert(err.message);
    }
});

// Delete VM
async function deleteVM(id) {
    if (!confirm(`Are you sure you want to destroy VM ${id}?`)) return;
    try {
        await fetch(`${API_URL}/${id}`, { method: 'DELETE' });
        fetchData();
    } catch {
        alert("Failed to terminate VM.");
    }
}

// Hibernate VM
async function hibernateVM(id) {
    try {
        await fetch(`${API_URL}/${id}/hibernate`, { method: 'POST' });
        fetchData();
    } catch {
        alert("Failed to hibernate VM.");
    }
}

// Wake VM
async function wakeVM(id) {
    try {
        await fetch(`${API_URL}/${id}/wake`, { method: 'POST' });
        fetchData();
    } catch {
        alert("Failed to wake VM.");
    }
}

// Update VM Ports
async function updatePorts(id, ssh, http, https) {
    try {
        await fetch(`${API_URL}/${id}/ports`, {
            method: 'PATCH',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ ssh_exposed: ssh, http_exposed: http, https_exposed: https })
        });
        fetchData();
    } catch {
        alert("Failed to update ports.");
    }
}

// Unlink VMs
async function unlinkVMs(vm1, vm2) {
    try {
        await fetch(`${LINKS_API_URL}/${vm1}/${vm2}`, { method: 'DELETE' });
        fetchData();
    } catch {
        alert("Failed to sever link.");
    }
}

// --- VOLUMES ---
function renderVolumes(volumes, vms) {
    if (!volumes || volumes.length === 0) {
        volumesContainer.innerHTML = `<p class="text-xs text-slate-400 italic">No volumes created yet.</p>`;
        return;
    }

    const prevSelections = {};
    volumes.forEach(vol => {
        const el = document.getElementById(`attach-select-${vol.id}`);
        if (el && el.value) prevSelections[vol.id] = el.value;
    });

    const vmOptions = vms.map(v => `<option value="${v.id}">VM ${v.id} (${v.ip})</option>`).join('');

    volumesContainer.innerHTML = volumes.map(vol => {
        const attached = vol.attached_vm_id !== -1;
        const statusBadge = attached 
            ? `<span class="inline-flex items-center px-2 py-0.5 rounded text-[10px] font-medium bg-green-100 text-green-800">In Use (VM ${vol.attached_vm_id})</span>`
            : `<span class="inline-flex items-center px-2 py-0.5 rounded text-[10px] font-medium bg-slate-100 text-slate-600">Available</span>`;

        let actionHtml = '';
        if (attached) {
            actionHtml = `
                <button onclick="detachVolume(${vol.id})" class="text-xs font-medium bg-orange-100 text-orange-700 hover:bg-orange-200 px-3 py-1.5 rounded-md transition-colors">
                    Detach
                </button>
            `;
        } else {
            actionHtml = `
                <div class="flex items-center space-x-2">
                    <select id="attach-select-${vol.id}" class="text-xs border border-slate-300 rounded-md px-2 py-1.5 bg-white focus:outline-none focus:ring-2 focus:ring-indigo-500 min-w-[120px]">
                        <option value="" disabled selected>Select VM...</option>
                        ${vmOptions}
                    </select>
                    <button onclick="attachVolume(${vol.id})" class="text-xs font-medium bg-indigo-600 text-white hover:bg-indigo-700 px-3 py-1.5 rounded-md transition-colors">
                        Attach
                    </button>
                </div>
            `;
        }

        return `
            <div class="flex items-center justify-between p-3 bg-white border border-slate-200 rounded-lg shadow-sm">
                <div>
                    <div class="flex items-center space-x-2">
                        <span class="font-semibold text-sm text-slate-800">${vol.name}</span>
                        <span class="text-xs text-slate-500 bg-slate-100 px-1.5 rounded">${vol.size_gb} GB</span>
                        ${statusBadge}
                    </div>
                    <div class="text-[10px] text-slate-400 font-mono mt-1">volumes/vol_${vol.id}.ext4</div>
                </div>
                <div class="flex items-center space-x-2">
                    ${actionHtml}
                    <button onclick="deleteVolume(${vol.id})" class="text-xs font-medium text-red-600 bg-red-50 hover:bg-red-100 px-3 py-1.5 rounded-md transition-colors border border-transparent hover:border-red-200">
                        Delete
                    </button>
                </div>
            </div>
        `;
    }).join('');

    volumes.forEach(vol => {
        if (prevSelections[vol.id]) {
            const el = document.getElementById(`attach-select-${vol.id}`);
            if (el) el.value = prevSelections[vol.id];
        }
    });
}

if (volumeForm) {
    volumeForm.addEventListener('submit', async (e) => {
        e.preventDefault();
        const btn = document.getElementById('vol-btn');
        btn.disabled = true;
        btn.textContent = "Creating...";

        const payload = {
            name: document.getElementById('vol-name').value.trim(),
            size_gb: parseInt(document.getElementById('vol-size').value)
        };

        try {
            const response = await fetch(VOLUMES_API_URL, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(payload)
            });
            if (!response.ok) throw new Error("Failed to create volume");
            document.getElementById('vol-name').value = '';
            document.getElementById('vol-size').value = '';
            fetchData();
        } catch (error) {
            alert(error.message);
        } finally {
            btn.disabled = false;
            btn.textContent = "Create Volume";
        }
    });
}

async function deleteVolume(id) {
    if (!confirm(`Are you sure you want to permanently delete Volume ${id}? All data will be lost!`)) return;
    try {
        await fetch(`${VOLUMES_API_URL}/${id}`, { method: 'DELETE' });
        fetchData();
    } catch {
        alert("Failed to delete volume.");
    }
}

async function attachVolume(volId) {
    const select = document.getElementById(`attach-select-${volId}`);
    const vmId = select.value;
    if (!vmId) {
        alert("Please select a VM first.");
        return;
    }
    
    if (!confirm(`Attaching this volume will REBOOT the target VM (VM ${vmId}). Unsaved RAM state will be lost. Proceed?`)) return;

    try {
        await fetch(`${VOLUMES_API_URL}/${volId}/attach`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ vm_id: parseInt(vmId) })
        });
        fetchData();
    } catch {
        alert("Failed to attach volume.");
    }
}

async function detachVolume(volId) {
    if (!confirm(`Detaching this volume will REBOOT the target VM. Proceed?`)) return;
    try {
        await fetch(`${VOLUMES_API_URL}/${volId}/detach`, { method: 'POST' });
        fetchData();
    } catch {
        alert("Failed to detach volume.");
    }
}

// Copy SSH command
function copySSH(btn, id) {
    const cmd = `ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ProxyCommand="nc -U /tmp/cryo_vm_${id}_22.sock" root@localhost`;
    navigator.clipboard.writeText(cmd).then(() => {
        const originalHtml = btn.innerHTML;
        btn.innerHTML = `<svg class="w-3 h-3 mr-1" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M5 13l4 4L19 7"></path></svg> Copied!`;
        btn.classList.replace('text-blue-600', 'text-green-600');
        btn.classList.replace('bg-blue-50', 'bg-green-50');
        btn.classList.replace('border-blue-200', 'border-green-200');
        setTimeout(() => {
            btn.innerHTML = originalHtml;
            btn.classList.replace('text-green-600', 'text-blue-600');
            btn.classList.replace('bg-green-50', 'bg-blue-50');
            btn.classList.replace('border-green-200', 'border-blue-200');
        }, 2000);
    });
}

// Poll every 2 seconds
fetchData();
setInterval(fetchData, 2000);

// Rootfs Slider
const rootfsGb = document.getElementById('rootfs_gb');
const rootfsDisplay = document.getElementById('rootfs_display');
if (rootfsGb && rootfsDisplay) {
    rootfsGb.addEventListener('input', (e) => {
        rootfsDisplay.textContent = e.target.value + ' GB';
    });
}
