const API_URL = 'http://localhost:8080/api/vms';

// DOM Elements
const vmTableBody = document.getElementById('vm-table-body');
const emptyState = document.getElementById('empty-state');
const spawnForm = document.getElementById('spawn-form');
const spawnBtn = document.getElementById('spawn-btn');
const connStatus = document.getElementById('connection-status');
const ipRadios = document.getElementsByName('ip-mode');
const manualSlotInput = document.getElementById('manual-slot');

// Toggle Manual IP Input
ipRadios.forEach(radio => {
    radio.addEventListener('change', (e) => {
        if (e.target.value === 'manual') {
            manualSlotInput.classList.remove('hidden');
            manualSlotInput.required = true;
        } else {
            manualSlotInput.classList.add('hidden');
            manualSlotInput.required = false;
            manualSlotInput.value = '';
        }
    });
});

// Fetch and render VMs
async function fetchVMs() {
    try {
        const response = await fetch(API_URL);
        if (!response.ok) throw new Error('Network response was not ok');
        const vms = await response.json();
        
        // Update connection status UI to green
        connStatus.innerHTML = `<span class="h-2 w-2 rounded-full bg-green-500 mr-2"></span> Connected`;
        connStatus.className = "flex items-center text-sm font-medium text-green-600";

        renderTable(vms);
    } catch (error) {
        // Update connection status UI to red
        connStatus.innerHTML = `<span class="h-2 w-2 rounded-full bg-red-500 mr-2"></span> Offline`;
        connStatus.className = "flex items-center text-sm font-medium text-red-600";
    }
}

// Render the VM table
function renderTable(vms) {
    if (vms.length === 0) {
        vmTableBody.innerHTML = '';
        emptyState.classList.remove('hidden');
        return;
    }

    emptyState.classList.add('hidden');
    vmTableBody.innerHTML = vms.map(vm => `
        <tr class="hover:bg-slate-50 transition-colors">
            <td class="px-6 py-4 whitespace-nowrap">
                <div class="font-medium text-slate-900">VM ${vm.id}</div>
                <div class="text-xs text-slate-500 font-mono">${vm.tap_name}</div>
            </td>
            <td class="px-6 py-4 whitespace-nowrap">
                ${vm.status === 'running' 
                    ? `<span class="inline-flex items-center px-2.5 py-0.5 rounded-full text-xs font-medium bg-green-100 text-green-800 border border-green-200">Running</span>`
                    : `<span class="inline-flex items-center px-2.5 py-0.5 rounded-full text-xs font-medium bg-blue-100 text-blue-800 border border-blue-200 status-booting">Booting...</span>`
                }
            </td>
            <td class="px-6 py-4 whitespace-nowrap">
                <div class="text-sm font-mono text-slate-700">${vm.ip}</div>
                ${vm.ssh_exposed ? `
                    <div class="mt-2 text-xs text-slate-500">LAN SSH Access:</div>
                    <div class="text-xs bg-slate-100 p-1 rounded font-mono text-blue-600 inline-block mt-0.5 select-all">
                        ssh root@${vm.host_lan_ip} -p ${vm.host_port}
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

// Spawn a new VM
spawnForm.addEventListener('submit', async (e) => {
    e.preventDefault();
    spawnBtn.disabled = true;
    spawnBtn.textContent = "Initializing...";

    const isManual = document.getElementById('ip-manual').checked;
    const payload = {
        vcpus: parseInt(document.getElementById('vcpus').value),
        mem_mib: parseInt(document.getElementById('ram').value),
        expose_ssh: document.getElementById('expose-ssh').checked,
        manual_slot: isManual ? parseInt(manualSlotInput.value) : null
    };

    try {
        await fetch(API_URL, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(payload)
        });
        
        // Reset form to defaults
        spawnForm.reset();
        manualSlotInput.classList.add('hidden');
        fetchVMs(); // Force immediate refresh
    } catch (error) {
        alert("Failed to spawn VM. Is the daemon running?");
    } finally {
        spawnBtn.disabled = false;
        spawnBtn.textContent = "Initialize VM";
    }
});

// Delete a VM
async function deleteVM(id) {
    if(!confirm(`Are you sure you want to destroy VM ${id}?`)) return;
    
    try {
        await fetch(`${API_URL}/${id}`, { method: 'DELETE' });
        fetchVMs(); // Force immediate refresh
    } catch (error) {
        alert("Failed to terminate VM.");
    }
}

// Start polling every 2 seconds
fetchVMs();
setInterval(fetchVMs, 2000);
