const API_URL = 'http://localhost:8080/api/vms';
const LINKS_API_URL = 'http://localhost:8080/api/links';

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

let cachedVMs = [];

// Fetch VMs and Links concurrently
async function fetchData() {
    try {
        const [vmsRes, linksRes] = await Promise.all([
            fetch(API_URL),
            fetch(LINKS_API_URL)
        ]);

        if (!vmsRes.ok || !linksRes.ok) throw new Error('Daemon communication error');

        const vms = await vmsRes.json();
        const links = await linksRes.json();
        cachedVMs = vms;

        connStatus.innerHTML = `<span class="h-2 w-2 rounded-full bg-green-500 mr-2"></span> Connected`;
        connStatus.className = "flex items-center text-sm font-medium text-green-600";

        renderTable(vms);
        updateLinkSelectors(vms);
        renderLinks(links, vms);
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
                    <div class="mt-1 text-xs bg-slate-100 p-1 rounded font-mono text-blue-600 inline-block select-all">
                        ssh root@${vm.host_lan_ip} -p${vm.host_port}
                    </div>
                ` : ''}
            </td>
            <td class="px-6 py-4 whitespace-nowrap text-sm text-slate-600">
                ${vm.vcpus} vCPU • ${vm.mem_mib} MB
            </td>
            <td class="px-6 py-4 whitespace-nowrap text-right">
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
        expose_ssh: document.getElementById('expose-ssh').checked
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

// Unlink VMs
async function unlinkVMs(vm1, vm2) {
    try {
        await fetch(`${LINKS_API_URL}/${vm1}/${vm2}`, { method: 'DELETE' });
        fetchData();
    } catch {
        alert("Failed to sever link.");
    }
}

// Poll every 2 seconds
fetchData();
setInterval(fetchData, 2000);

