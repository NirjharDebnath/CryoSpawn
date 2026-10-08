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
        const hex = vm.uuid.substring(3);

        if (vm.status === 'running') {
            statusBadge = `<span class="inline-flex items-center px-2 py-0.5 rounded text-[10px] font-medium bg-green-100 text-green-800"><span class="w-1.5 h-1.5 rounded-full bg-green-500 mr-1.5"></span>Running</span>`;
            
            actionButtons = ``;
            // Only show SSH copy button if SSH port is actually toggled ON
            if (vm.ssh_exposed) {
                actionButtons += `
                    <button onclick="copySSH(this, '${vm.uuid}')" class="text-xs font-medium text-blue-600 bg-blue-50 hover:bg-blue-100 px-3 py-1.5 rounded-md transition-colors border border-blue-200">
                        <svg class="w-3 h-3 mr-1 inline" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M8 16H6a2 2 0 01-2-2V6a2 2 0 012-2h8a2 2 0 012 2v2m-6 12h8a2 2 0 002-2v-8a2 2 0 00-2-2h-8a2 2 0 00-2 2v8a2 2 0 002 2z"></path></svg>
                        SSH
                    </button>
                `;
            }
            actionButtons += `
                <button onclick="hibernateVM('${vm.uuid}')" class="text-xs font-medium text-orange-600 bg-orange-50 hover:bg-orange-100 px-3 py-1.5 rounded-md transition-colors border border-transparent hover:border-orange-200">
                    Hibernate
                </button>
            `;
        } else if (vm.status === 'hibernated') {
            statusBadge = `<span class="inline-flex items-center px-2 py-0.5 rounded text-[10px] font-medium bg-orange-100 text-orange-800"><span class="w-1.5 h-1.5 rounded-full bg-orange-500 mr-1.5"></span>Hibernated</span>`;
            actionButtons = `
                <button onclick="wakeVM('${vm.uuid}')" class="text-xs font-medium text-green-600 bg-green-50 hover:bg-green-100 px-3 py-1.5 rounded-md transition-colors border border-transparent hover:border-green-200">
                    Wake VM
                </button>
            `;
        } else {
            statusBadge = `<span class="inline-flex items-center px-2 py-0.5 rounded text-[10px] font-medium bg-slate-100 text-slate-800"><span class="w-1.5 h-1.5 rounded-full bg-slate-400 mr-1.5"></span>${vm.status}</span>`;
            actionButtons = `<span class="text-xs text-slate-400 italic">Processing...</span>`;
        }

        let domains = '';
        if (vm.http_exposed) domains += `<div class="mt-1"><a href="http://cryo-${hex}.cryo" target="_blank" class="text-blue-500 hover:underline text-[10px]">http://cryo-${hex}.cryo</a></div>`;
        if (vm.https_exposed) domains += `<div class="mt-0.5"><a href="https://cryo-${hex}.cryo" target="_blank" class="text-green-500 hover:underline text-[10px]">https://cryo-${hex}.cryo</a></div>`;

        // Interactive port toggles
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
            if (!response.ok) throw new Error("Server rejected request");
            fetchData();
        } catch (error) {
            alert("Failed to spawn VM: " + error.message);
        } finally {
            spawnBtn.disabled = false;
            spawnBtn.textContent = "Initialize VM";
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
        if (!vm1_uuid || !vm2_uuid || vm1_uuid === vm2_uuid) { alert("Please select two different VMs."); return; }
        
        try {
            await fetch(LINKS_API_URL, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ vm1_id: vm1_uuid, vm2_id: vm2_uuid })
            });
            fetchData();
        } catch {
            alert("Failed to bridge VMs.");
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

    const vmOptions = vms.map(v => `<option value="${v.uuid}">${v.name || "VM " + v.id} (${v.ip})</option>`).join('');

    volumesContainer.innerHTML = volumes.map(vol => {
        const attached = vol.attached_vm_id !== -1;
        const statusBadge = attached 
            ? `<span class="inline-flex items-center px-2 py-0.5 rounded text-[10px] font-medium bg-green-100 text-green-800">In Use</span>`
            : `<span class="inline-flex items-center px-2 py-0.5 rounded text-[10px] font-medium bg-slate-100 text-slate-600">Available</span>`;

        let actionHtml = attached ? `
            <button onclick="detachVolume(${vol.id})" class="text-xs font-medium bg-orange-100 text-orange-700 hover:bg-orange-200 px-3 py-1.5 rounded-md">Detach</button>
        ` : `
            <div class="flex items-center space-x-2">
                <select id="attach-select-${vol.id}" class="text-xs border rounded-md px-2 py-1.5">${vmOptions}</select>
                <button onclick="attachVolume(${vol.id})" class="text-xs font-medium bg-indigo-600 text-white hover:bg-indigo-700 px-3 py-1.5 rounded-md">Attach</button>
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
                    <button onclick="deleteVolume(${vol.id})" class="text-xs text-red-600 bg-red-50 hover:bg-red-100 px-3 py-1.5 rounded-md">Delete</button>
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
        const payload = {
            name: document.getElementById('vol-name').value.trim(),
            size_gb: parseInt(document.getElementById('vol-size').value)
        };
        try {
            await fetch(VOLUMES_API_URL, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(payload)
            });
            document.getElementById('vol-name').value = '';
            document.getElementById('vol-size').value = '';
            fetchData();
        } finally { btn.disabled = false; }
    });
}


async function updatePorts(uuid, ssh, http, https) {
    try {
        await fetch(`${API_URL}/${uuid}/ports`, {
            method: 'PATCH',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ ssh_exposed: ssh, http_exposed: http, https_exposed: https })
        });
        fetchData();
    } catch {
        alert("Failed to update ports.");
    }
}

async function deleteVM(uuid) { if (confirm("Delete VM?")) { await fetch(`${API_URL}/${uuid}`, {method:'DELETE'}); fetchData(); } }
async function hibernateVM(uuid) { await fetch(`${API_URL}/${uuid}/hibernate`, {method:'POST'}); fetchData(); }
async function wakeVM(uuid) { await fetch(`${API_URL}/${uuid}/wake`, {method:'POST'}); fetchData(); }
async function unlinkVMs(vm1, vm2) { await fetch(`${LINKS_API_URL}/${vm1}/${vm2}`, {method:'DELETE'}); fetchData(); }
async function deleteVolume(id) { if (confirm("Delete Volume?")) { await fetch(`${VOLUMES_API_URL}/${id}`, {method:'DELETE'}); fetchData(); } }
async function attachVolume(vol_id) {
    const vm_uuid = document.getElementById(`attach-select-${vol_id}`).value;
    if (confirm("Attaching will REBOOT the target VM. Proceed?")) {
        await fetch(`${VOLUMES_API_URL}/${vol_id}/attach`, { method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify({vm_id: vm_uuid})});
        fetchData();
    }
}
async function detachVolume(vol_id) {
    if (confirm("Detaching will REBOOT the target VM. Proceed?")) {
        await fetch(`${VOLUMES_API_URL}/${vol_id}/detach`, {method:'POST'});
        fetchData();
    }
}
function copySSH(btn, uuid) {
    const hex = uuid.substring(3);
    const cmd = `ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ProxyCommand="nc -U /tmp/cryo-${hex}_22.sock" root@localhost`;
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
