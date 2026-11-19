const API_URL = '/api/vms';
const LINKS_API_URL = '/api/links';
const VOLUMES_API_URL = '/api/volumes';

const vmTableBody = document.getElementById('vm-table-body');
const emptyState = document.getElementById('empty-state');
const spawnForm = document.getElementById('spawn-form');
const spawnBtn = document.getElementById('spawn-btn');
const vmCountBadge = document.getElementById('vm-count-badge');

const linkForm = document.getElementById('link-form');
const linkVm1 = document.getElementById('link-vm1');
const linkVm2 = document.getElementById('link-vm2');
const linksContainer = document.getElementById('links-container');

const volumeForm = document.getElementById('volume-form');
const volumesContainer = document.getElementById('volumes-container');

let cachedVMs = [];
let cachedVolumes = [];
let lastRenderState = "";

async function fetchData() {
    try {
        const [vmsRes, linksRes, volsRes] = await Promise.all([
            fetch(API_URL),
            fetch(LINKS_API_URL),
            fetch(VOLUMES_API_URL)
        ]);

        if (vmsRes.status === 401) { window.location.href = '/login'; return; }
        if (!vmsRes.ok || !linksRes.ok || !volsRes.ok) throw new Error('Daemon communication error');

        const vms = await vmsRes.json();
        const links = await linksRes.json();
        const volumes = await volsRes.json();
        cachedVMs = vms;
        cachedVolumes = volumes;

        const currentState = JSON.stringify({vms, links, volumes});
        if (currentState !== lastRenderState) {
            renderTable(vms);
            updateLinkSelectors(vms);
            renderLinks(links, vms);
            renderVolumes(volumes, vms);
            lastRenderState = currentState;
        }
    } catch (error) {
        console.error("Fetch Data Error:", error);
    }
}

function renderTable(vms) {
    if (vmCountBadge) vmCountBadge.textContent = `${vms.length} VM${vms.length === 1 ? '' : 's'}`;
    
    if (vms.length === 0) {
        if (vmTableBody) vmTableBody.innerHTML = '';
        if (emptyState) emptyState.classList.remove('hidden');
        return;
    }
    if (emptyState) emptyState.classList.add('hidden');

    const html = vms.map(vm => {
        let statusBadge = '';
        let actionButtons = '';
        const hex = vm.uuid.startsWith('vm-') ? vm.uuid.substring(3) : vm.uuid;

        if (vm.status === 'running') {
            statusBadge = `<span class="inline-flex items-center px-2 py-0.5 rounded text-[10px] font-medium bg-green-100 text-green-800"><span class="w-1.5 h-1.5 rounded-full bg-green-500 mr-1.5"></span>Running</span>`;
            
            actionButtons = ``;
            if (vm.ssh_exposed) {
                actionButtons += `
                    <button onclick="copySSH(this, '${vm.uuid}', ${vm.host_port || 0})" class="text-xs font-medium text-blue-600 bg-blue-50 hover:bg-blue-100 px-3 py-1.5 rounded-md transition-colors border border-blue-200">
                        <svg class="w-3 h-3 mr-1 inline" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M8 16H6a2 2 0 01-2-2V6a2 2 0 012-2h8a2 2 0 012 2v2m-6 12h8a2 2 0 002-2v-8a2 2 0 00-2-2h-8a2 2 0 00-2 2v8a2 2 0 002 2z"></path></svg>
                        SSH
                    </button>
                `;
            }
            actionButtons += `
                <button onclick="hibernateVM('${vm.uuid}')" class="text-xs font-medium text-orange-600 bg-orange-50 hover:bg-orange-100 px-3 py-1.5 rounded-md transition-colors border border-transparent hover:border-orange-200">
                    Hibernate
                </button>
                <button onclick="restartVM('${vm.uuid}')" class="text-xs font-medium text-slate-600 bg-slate-100 hover:bg-slate-200 px-3 py-1.5 rounded-md transition-colors border border-transparent hover:border-slate-300">
                    Restart
                </button>
            `;
        } else if (vm.status === 'hibernated') {
            statusBadge = `<span class="inline-flex items-center px-2 py-0.5 rounded text-[10px] font-medium bg-orange-100 text-orange-800"><span class="w-1.5 h-1.5 rounded-full bg-orange-500 mr-1.5"></span>Hibernated</span>`;
            actionButtons = `
                <button onclick="wakeVM('${vm.uuid}')" class="text-xs font-medium text-green-600 bg-green-50 hover:bg-green-100 px-3 py-1.5 rounded-md transition-colors border border-transparent hover:border-green-200">
                    Wake VM
                </button>
            `;
        } else if (vm.status === 'stopped') {
            statusBadge = `<span class="inline-flex items-center px-2 py-0.5 rounded text-[10px] font-medium bg-slate-200 text-slate-800"><span class="w-1.5 h-1.5 rounded-full bg-slate-500 mr-1.5"></span>Stopped</span>`;
            actionButtons = `
                <button onclick="wakeVM('${vm.uuid}')" class="text-xs font-medium text-green-600 bg-green-50 hover:bg-green-100 px-3 py-1.5 rounded-md transition-colors border border-transparent hover:border-green-200">
                    Start VM
                </button>
            `;
        } else {
            statusBadge = `<span class="inline-flex items-center px-2 py-0.5 rounded text-[10px] font-medium bg-slate-100 text-slate-800"><span class="w-1.5 h-1.5 rounded-full bg-slate-400 mr-1.5"></span>${vm.status}</span>`;
            actionButtons = `<span class="text-xs text-slate-400 italic">Processing...</span>`;
        }

        let domains = '';
        if (vm.http_exposed) domains += `<div class="mt-1"><a href="http://cryo-${hex}.cryo" target="_blank" class="text-blue-500 hover:underline text-[10px]">http://cryo-${hex}.cryo</a></div>`;
        if (vm.https_exposed) domains += `<div class="mt-0.5"><a href="https://cryo-${hex}.cryo" target="_blank" class="text-green-500 hover:underline text-[10px]">https://cryo-${hex}.cryo</a></div>`;

        let hostPortInfo = '';
        if (vm.ssh_exposed && vm.host_port > 0) {
            hostPortInfo = `<div class="text-[10px] font-mono text-blue-600 mt-1">SSH Port: ${vm.host_port}</div>`;
        }

        const toggleUI = `
            <div class="mt-3 space-y-2">
                <label class="flex items-center cursor-pointer">
                    <input type="checkbox" class="sr-only peer" ${vm.ssh_exposed ? 'checked' : ''} onchange="updatePorts('${vm.uuid}', this.checked, ${vm.http_exposed}, ${vm.https_exposed})">
                    <div class="w-7 h-4 bg-slate-200 peer-focus:outline-none rounded-full peer peer-checked:after:translate-x-full peer-checked:after:border-white after:content-[''] after:absolute after:top-[2px] after:left-[2px] after:bg-white after:border-gray-300 after:border after:rounded-full after:h-3 after:w-3 after:transition-all peer-checked:bg-blue-600 relative"></div>
                    <span class="ml-2 text-[10px] font-medium text-slate-600">SSH (22)</span>
                </label>
                <label class="flex items-center cursor-pointer">
                    <input type="checkbox" class="sr-only peer" ${vm.http_exposed ? 'checked' : ''} onchange="updatePorts('${vm.uuid}', ${vm.ssh_exposed}, this.checked, ${vm.https_exposed})">
                    <div class="w-7 h-4 bg-slate-200 peer-focus:outline-none rounded-full peer peer-checked:after:translate-x-full peer-checked:after:border-white after:content-[''] after:absolute after:top-[2px] after:left-[2px] after:bg-white after:border-gray-300 after:border after:rounded-full after:h-3 after:w-3 after:transition-all peer-checked:bg-green-600 relative"></div>
                    <span class="ml-2 text-[10px] font-medium text-slate-600">HTTP (80)</span>
                </label>
                <label class="flex items-center cursor-pointer">
                    <input type="checkbox" class="sr-only peer" ${vm.https_exposed ? 'checked' : ''} onchange="updatePorts('${vm.uuid}', ${vm.ssh_exposed}, ${vm.http_exposed}, this.checked)">
                    <div class="w-7 h-4 bg-slate-200 peer-focus:outline-none rounded-full peer peer-checked:after:translate-x-full peer-checked:after:border-white after:content-[''] after:absolute after:top-[2px] after:left-[2px] after:bg-white after:border-gray-300 after:border after:rounded-full after:h-3 after:w-3 after:transition-all peer-checked:bg-green-600 relative"></div>
                    <span class="ml-2 text-[10px] font-medium text-slate-600">HTTPS (443)</span>
                </label>
            </div>
        `;

        return `
        <tr>
            <td class="px-6 py-4">
                <div class="font-medium text-slate-900">${vm.name || "VM " + vm.id}</div>
                <div class="text-[10px] text-slate-400 font-mono mt-0.5">${vm.uuid}</div>
                <div class="text-xs text-slate-500 mt-1"><span class="font-medium">Project:</span> ${vm.project || "default"}</div>
            </td>
            <td class="px-6 py-4">${statusBadge}</td>
            <td class="px-6 py-4 font-mono text-xs text-slate-600">
                <div>${vm.ip}</div>
                <div class="text-[10px] text-slate-400 mt-1">Host: ${vm.host_ip}</div>
            </td>
            <td class="px-6 py-4">
                <div class="text-xs text-slate-700 font-medium">${vm.vcpus} vCPU / ${vm.mem_mib} MB RAM</div>
                <div class="text-[10px] text-slate-500 mt-0.5">${vm.rootfs_gb} GB Disk</div>
                ${hostPortInfo}
                ${toggleUI}
                ${domains}
            </td>
            <td class="px-6 py-4 text-right space-x-2">
                ${actionButtons}
                <button onclick="deleteVM('${vm.uuid}')" class="text-xs font-medium text-red-600 bg-red-50 hover:bg-red-100 px-3 py-1.5 rounded-md transition-colors border border-transparent hover:border-red-200">
                    Delete
                </button>
            </td>
        </tr>
        `;
    }).join('');

    if (vmTableBody) vmTableBody.innerHTML = html;
}

if (spawnForm) {
    spawnForm.addEventListener('submit', async (e) => {
        e.preventDefault();
        spawnBtn.disabled = true;
        spawnBtn.textContent = "Initializing...";

        const payload = {
            project: document.getElementById('project-name').value.trim() || 'default',
            name: document.getElementById('vm-name').value.trim(),
            vcpus: parseInt(document.getElementById('vcpus').value),
            mem_mib: parseInt(document.getElementById('ram').value),
            expose_ssh: document.getElementById('expose-ssh') ? document.getElementById('expose-ssh').checked : false,
            expose_http: document.getElementById('expose-http') ? document.getElementById('expose-http').checked : false,
            expose_https: document.getElementById('expose-https') ? document.getElementById('expose-https').checked : false,
            rootfs_gb: parseInt(document.getElementById('rootfs_gb').value)
        };

        try {
            const response = await fetch(API_URL, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(payload)
            });
            if (!response.ok) {
                const data = await response.json().catch(() => ({}));
                throw new Error(data.error || "Server rejected request");
            }
            fetchData();
        } catch (error) {
            alert("Failed to spawn VM: " + error.message);
        } finally {
            spawnBtn.disabled = false;
            spawnBtn.textContent = "Deploy MicroVM";
        }
    });
}

function updateLinkSelectors(vms) {
    if (!linkVm1 || !linkVm2) return;
    const runningVMs = vms.filter(v => v.status === 'running');
    const prev1 = linkVm1.value;
    const prev2 = linkVm2.value;

    const options = runningVMs.map(vm => `<option value="${vm.uuid}">${vm.name || 'VM ' + vm.id} (${vm.project || 'default'} - ${vm.ip})</option>`).join('');
    linkVm1.innerHTML = options;
    linkVm2.innerHTML = options;
    if (prev1) linkVm1.value = prev1;
    if (prev2) linkVm2.value = prev2;
}

function renderLinks(links, vms) {
    if (!linksContainer) return;
    if (links.length === 0) {
        linksContainer.innerHTML = `<p class="text-xs text-slate-400 italic">No custom routes active. VMs are currently isolated.</p>`;
        return;
    }

    const getName = (uuid) => {
        const vm = vms.find(v => v.uuid === uuid);
        return vm ? (vm.name || 'VM ' + vm.id) : uuid;
    };

    linksContainer.innerHTML = links.map(l => `
        <div class="flex items-center justify-between p-2 bg-slate-50 border border-slate-100 rounded text-sm">
            <div class="flex items-center space-x-3 text-slate-700">
                <span class="font-medium text-xs px-2 py-1 bg-white border border-slate-200 rounded">${getName(l.vm1_uuid)}</span>
                <span class="text-slate-400 text-xs font-bold">⟷</span>
                <span class="font-medium text-xs px-2 py-1 bg-white border border-slate-200 rounded">${getName(l.vm2_uuid)}</span>
            </div>
            <button onclick="unlinkVMs('${l.vm1_uuid}', '${l.vm2_uuid}')" class="text-xs text-red-500 hover:text-red-700 hover:bg-red-50 px-2 py-1 rounded transition-colors">Sever Link</button>
        </div>
    `).join('');
}

if (linkForm) {
    linkForm.addEventListener('submit', async (e) => {
        e.preventDefault();
        const vm1_uuid = linkVm1.value;
        const vm2_uuid = linkVm2.value;
        if (!vm1_uuid || !vm2_uuid || vm1_uuid === vm2_uuid) { 
            alert("Please select two different running VMs to bridge."); 
            return; 
        }
        
        try {
            const res = await fetch(LINKS_API_URL, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ vm1_id: vm1_uuid, vm2_id: vm2_uuid })
            });
            const data = await res.json();
            if (!res.ok) {
                alert("Failed to bridge VMs: " + (data.error || "Unknown error"));
            } else {
                fetchData();
            }
        } catch (err) {
            alert("Failed to bridge VMs: " + err.message);
        }
    });
}

function renderVolumes(volumes, vms) {
    if (!volumesContainer) return;
    if (!volumes || volumes.length === 0) {
        volumesContainer.innerHTML = `<p class="text-xs text-slate-400 italic">No volumes created yet.</p>`;
        return;
    }

    const prevSelections = {};
    volumes.forEach(vol => {
        const el = document.getElementById(`attach-select-${vol.id}`);
        if (el && el.value) prevSelections[vol.id] = el.value;
    });

    const hasVMs = vms.length > 0;
    const vmOptions = hasVMs 
        ? vms.map(v => `<option value="${v.uuid}">${v.name || "VM " + v.id} (${v.ip})</option>`).join('')
        : `<option value="" disabled selected>No VMs available</option>`;

    volumesContainer.innerHTML = volumes.map(vol => {
        const attached = vol.attached_vm_id !== -1;
        const attachedText = (vol.attached_vm_name || vol.attached_vm_uuid) 
            ? `Attached to: ${vol.attached_vm_name || vol.attached_vm_uuid}`
            : 'In Use';

        const statusBadge = attached 
            ? `<span class="inline-flex items-center px-2 py-0.5 rounded text-[10px] font-medium bg-green-100 text-green-800">${attachedText}</span>`
            : `<span class="inline-flex items-center px-2 py-0.5 rounded text-[10px] font-medium bg-slate-100 text-slate-600">Available</span>`;

        let actionHtml = attached ? `
            <button onclick="detachVolume(${vol.id})" class="text-xs font-medium bg-orange-100 text-orange-700 hover:bg-orange-200 px-3 py-1.5 rounded-md transition-colors">Detach</button>
        ` : `
            <div class="flex items-center space-x-2">
                <select id="attach-select-${vol.id}" class="text-xs border rounded-md px-2 py-1.5 focus:outline-none focus:ring-1 focus:ring-blue-500" ${hasVMs ? '' : 'disabled'}>${vmOptions}</select>
                <button onclick="attachVolume(${vol.id})" class="text-xs font-medium bg-indigo-600 text-white hover:bg-indigo-700 px-3 py-1.5 rounded-md transition-colors" ${hasVMs ? '' : 'disabled'}>Attach</button>
            </div>
        `;

        return `
            <div class="flex items-center justify-between p-3 bg-white border border-slate-200 rounded-lg shadow-sm">
                <div>
                    <div class="flex items-center space-x-2">
                        <span class="font-semibold text-sm text-slate-800">${vol.name}</span>
                        <span class="text-xs text-slate-500 bg-slate-100 px-1.5 rounded">${vol.size_gb} GB</span>
                        ${statusBadge}
                    </div>
                </div>
                <div class="flex items-center space-x-2">
                    ${actionHtml}
                    <button onclick="deleteVolume(${vol.id})" class="text-xs text-red-600 bg-red-50 hover:bg-red-100 px-3 py-1.5 rounded-md transition-colors">Delete</button>
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
        const name = document.getElementById('vol-name').value.trim();
        const size_gb = parseInt(document.getElementById('vol-size').value);

        if (!name || isNaN(size_gb) || size_gb < 1 || size_gb > 1000) {
            alert("Please enter a valid volume name and a size between 1 and 1000 GB.");
            btn.disabled = false;
            return;
        }

        try {
            const res = await fetch(VOLUMES_API_URL, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ name, size_gb })
            });
            const data = await res.json();
            if (res.ok) {
                document.getElementById('vol-name').value = '';
                document.getElementById('vol-size').value = '';
                fetchData();
            } else {
                alert("Failed to create volume: " + (data.error || "Unknown error"));
            }
        } catch (err) {
            alert("Network error: " + err.message);
        } finally { btn.disabled = false; }
    });
}

async function updatePorts(uuid, ssh, http, https) {
    try {
        const res = await fetch(`${API_URL}/${uuid}/ports`, {
            method: 'PATCH',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ ssh_exposed: ssh, http_exposed: http, https_exposed: https })
        });
        const data = await res.json();
        if (!res.ok) {
            alert("Failed to update ports: " + (data.error || "Unknown error"));
        }
        fetchData();
    } catch (err) {
        alert("Failed to update ports: " + err.message);
    }
}

async function deleteVM(uuid) { 
    if (confirm("Permanently delete this microVM and free disk resources?")) { 
        const res = await fetch(`${API_URL}/${uuid}`, {method:'DELETE'}); 
        if (!res.ok) {
            const data = await res.json().catch(() => ({}));
            alert("Failed to delete VM: " + (data.error || "Unknown error"));
        }
        fetchData(); 
    } 
}

async function restartVM(uuid) {
    if (confirm("Restart this microVM?")) {
        const res = await fetch(`${API_URL}/${uuid}/restart`, {method:'POST'});
        if (!res.ok) {
            const data = await res.json().catch(() => ({}));
            alert("Failed to restart VM: " + (data.error || "Unknown error"));
        }
        fetchData();
    }
}

async function hibernateVM(uuid) { 
    const res = await fetch(`${API_URL}/${uuid}/hibernate`, {method:'POST'}); 
    if (!res.ok) {
        const data = await res.json().catch(() => ({}));
        alert("Failed to hibernate VM: " + (data.error || "Unknown error"));
    }
    fetchData(); 
}

async function wakeVM(uuid) { 
    const res = await fetch(`${API_URL}/${uuid}/wake`, {method:'POST'}); 
    if (!res.ok) {
        const data = await res.json().catch(() => ({}));
        alert("Failed to start/wake VM: " + (data.error || "Unknown error"));
    }
    fetchData(); 
}

async function unlinkVMs(vm1, vm2) { 
    const res = await fetch(`${LINKS_API_URL}/${vm1}/${vm2}`, {method:'DELETE'}); 
    if (!res.ok) {
        const data = await res.json().catch(() => ({}));
        alert("Failed to unlink VMs: " + (data.error || "Unknown error"));
    }
    fetchData(); 
}

async function deleteVolume(id) { 
    if (confirm("Delete this volume?")) { 
        const res = await fetch(`${VOLUMES_API_URL}/${id}`, {method:'DELETE'}); 
        const data = await res.json().catch(() => ({}));
        if (!res.ok) {
            alert("Failed to delete volume: " + (data.error || "Unknown error"));
        }
        fetchData(); 
    } 
}

async function attachVolume(vol_id) {
    const select = document.getElementById(`attach-select-${vol_id}`);
    const vm_uuid = select ? select.value : "";
    if (!vm_uuid) {
        alert("Please select a target MicroVM to attach this volume to.");
        return;
    }
    if (confirm("Attaching will reload the target VM to mount the block device. Proceed?")) {
        const res = await fetch(`${VOLUMES_API_URL}/${vol_id}/attach`, { 
            method: 'POST', 
            headers: {'Content-Type': 'application/json'}, 
            body: JSON.stringify({vm_id: vm_uuid})
        });
        const data = await res.json().catch(() => ({}));
        if (!res.ok) {
            alert("Failed to attach volume: " + (data.error || "Unknown error"));
        }
        fetchData();
    }
}

async function detachVolume(vol_id) {
    if (confirm("Detaching will reload the target VM to safely unmount the drive. Proceed?")) {
        const res = await fetch(`${VOLUMES_API_URL}/${vol_id}/detach`, {method:'POST'});
        const data = await res.json().catch(() => ({}));
        if (!res.ok) {
            alert("Failed to detach volume: " + (data.error || "Unknown error"));
        }
        fetchData();
    }
}

function copySSH(btn, uuid, hostPort) {
    const hex = uuid.startsWith('vm-') ? uuid.substring(3) : uuid;
    let cmd = "";
    if (hostPort && parseInt(hostPort) > 0) {
        cmd = `ssh -p ${hostPort} root@localhost`;
    } else {
        cmd = `ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ProxyCommand="nc -U /tmp/cryo-${hex}_22.sock" root@localhost`;
    }

    navigator.clipboard.writeText(cmd).then(() => {
        const originalHtml = btn.innerHTML;
        btn.innerHTML = `<svg class="w-3 h-3 mr-1 inline" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M5 13l4 4L19 7"></path></svg> Copied!`;
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

fetchData();
setInterval(fetchData, 2000);

const rootfsGb = document.getElementById('rootfs_gb');
const rootfsDisplay = document.getElementById('rootfs_display');
if (rootfsGb && rootfsDisplay) {
    rootfsGb.addEventListener('input', (e) => rootfsDisplay.textContent = e.target.value + ' GB');
}
