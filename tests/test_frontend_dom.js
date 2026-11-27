// Headless DOM unit test for app.js UI and selector logic
const assert = require('assert');
const fs = require('fs');
const path = require('path');

// Minimal DOM Mock
class Element {
    constructor(id = '') {
        this.id = id;
        this.innerHTML = '';
        this.value = '';
        this.checked = false;
        this.disabled = false;
        this.dataset = {};
        this.classList = {
            classes: new Set(),
            add(c) { this.classes.add(c); },
            remove(c) { this.classes.delete(c); },
            contains(c) { return this.classes.has(c); }
        };
        this.listeners = {};
    }
    focus() {}
    select() {}
    setSelectionRange() {}
    setAttribute() {}
    addEventListener(evt, fn) {
        if (!this.listeners[evt]) this.listeners[evt] = [];
        this.listeners[evt].push(fn);
    }
    dispatchEvent(evt) {
        if (this.listeners[evt]) {
            this.listeners[evt].forEach(fn => fn({ preventDefault: () => {} }));
        }
    }
}

const elements = {
    'vm-table-body': new Element('vm-table-body'),
    'empty-state': new Element('empty-state'),
    'spawn-form': new Element('spawn-form'),
    'spawn-btn': new Element('spawn-btn'),
    'vm-count-badge': new Element('vm-count-badge'),
    'link-form': new Element('link-form'),
    'link-btn': new Element('link-btn'),
    'link-vm1': new Element('link-vm1'),
    'link-vm2': new Element('link-vm2'),
    'links-container': new Element('links-container'),
    'volume-form': new Element('volume-form'),
    'volumes-container': new Element('volumes-container'),
    'routing-permissions-container': new Element('routing-permissions-container'),
    'project-name': new Element('project-name'),
    'vm-name': new Element('vm-name'),
    'vcpus': new Element('vcpus'),
    'ram': new Element('ram'),
    'rootfs_gb': new Element('rootfs_gb'),
    'allow-interproject': new Element('allow-interproject'),
    'projects-grid': new Element('projects-grid'),
    'projects-empty-state': new Element('projects-empty-state'),
    'projects-count-badge': new Element('projects-count-badge'),
    'summary-projects-count': new Element('summary-projects-count'),
    'summary-vms-count': new Element('summary-vms-count'),
    'summary-running-count': new Element('summary-running-count'),
    'project-options': new Element('project-options'),
    'project-page-title': new Element('project-page-title'),
    'project-description': new Element('project-description'),
    'project-status-summary': new Element('project-status-summary'),
    'project-not-found': new Element('project-not-found'),
    'project-workspace': new Element('project-workspace'),
    'delete-project-btn': new Element('delete-project-btn'),
    'create-missing-project-btn': new Element('create-missing-project-btn'),
    'daemon-status-banner': new Element('daemon-status-banner'),
    'ssh-modal': new Element('ssh-modal'),
    'ssh-modal-input': new Element('ssh-modal-input'),
    'ssh-modal-copy-btn': new Element('ssh-modal-copy-btn')
};

global.document = {
    getElementById(id) {
        return elements[id] || null;
    },
    createElement() { return { style: {}, focus() {}, select() {}, setSelectionRange() {}, setAttribute() {} }; },
    body: { appendChild() {}, removeChild() {} },
    execCommand() { return true; }
};

global.navigator = {};
global.window = {
    location: { href: '' },
    isSecureContext: false
};
global.fetch = () => Promise.resolve({ ok: true, json: () => Promise.resolve([]) });
global.setInterval = () => {};

// Load app.js
const appJsPath = path.join(__dirname, '../internal/ui/app.js');
const appJsCode = fs.readFileSync(appJsPath, 'utf8');

// Evaluate app.js in this context
eval(appJsCode);

console.log("[TEST] Running Frontend JavaScript DOM tests...");

// Test 1: updateLinkSelectors & updateDestinationSelector with isolated projects
const vms = [
    { id: 0, uuid: 'vm-alpha', name: 'Alpha', ip: '172.16.0.2', status: 'running', project: 'proj-A', allow_interproject_routing: false },
    { id: 1, uuid: 'vm-beta', name: 'Beta', ip: '172.16.0.6', status: 'running', project: 'proj-B', allow_interproject_routing: false },
    { id: 2, uuid: 'vm-alpha-2', name: 'Alpha-2', ip: '172.16.0.10', status: 'running', project: 'proj-A', allow_interproject_routing: false }
];

updateLinkSelectors(vms);

// linkVm1 should have options for Alpha, Beta, Alpha-2
const vm1Html = elements['link-vm1'].innerHTML;
assert(vm1Html.includes('value="vm-alpha"'));
assert(vm1Html.includes('value="vm-beta"'));
assert(vm1Html.includes('value="vm-alpha-2"'));
assert(vm1Html.includes('Project: proj-A'));
assert(vm1Html.includes('Project: proj-B'));

// Set linkVm1 to vm-alpha
elements['link-vm1'].value = 'vm-alpha';
updateDestinationSelector(vms);

// vm-alpha is in proj-A. Destination should ONLY show Alpha-2 (same project)
// vm-beta is in proj-B and neither has opted in -> MUST NOT APPEAR
const vm2Html = elements['link-vm2'].innerHTML;
assert(vm2Html.includes('Same Project (proj-A)'));
assert(vm2Html.includes('value="vm-alpha-2"'));
assert(!vm2Html.includes('value="vm-beta"'), "vm-beta must NOT appear as link target when allow_interproject_routing is false");
console.log("  [✓] updateDestinationSelector excludes cross-project VMs without opt-in");

// Test 2: Opt-in only Alpha, Beta still false -> Beta must STILL not appear
vms[0].allow_interproject_routing = true;
updateDestinationSelector(vms);
const vm2Html_oneOpted = elements['link-vm2'].innerHTML;
assert(!vm2Html_oneOpted.includes('value="vm-beta"'), "vm-beta must NOT appear when target VM has not opted in");
console.log("  [✓] updateDestinationSelector excludes cross-project VMs when only source opted in");

// Test 3: Opt-in both Alpha and Beta -> Beta MUST appear under Inter-Project Authorized
vms[1].allow_interproject_routing = true;
updateDestinationSelector(vms);
const vm2Html_bothOpted = elements['link-vm2'].innerHTML;
assert(vm2Html_bothOpted.includes('Inter-Project Authorized (proj-B)'));
assert(vm2Html_bothOpted.includes('value="vm-beta"'));
assert(vm2Html_bothOpted.includes('Same Project (proj-A)'));
assert(vm2Html_bothOpted.includes('value="vm-alpha-2"'));
console.log("  [✓] updateDestinationSelector includes cross-project VMs when both have opted in");

// Test 4: Switching source VM dynamically updates destinations
elements['link-vm1'].value = 'vm-beta';
updateDestinationSelector(vms);
const vm2Html_betaSrc = elements['link-vm2'].innerHTML;
// Beta is proj-B. Same project has none. Inter-Project should show Alpha (opted in), but NOT Alpha-2 (proj-A, opted out)
assert(vm2Html_betaSrc.includes('Inter-Project Authorized (proj-A)'));
assert(vm2Html_betaSrc.includes('value="vm-alpha"'));
assert(!vm2Html_betaSrc.includes('value="vm-alpha-2"'), "Alpha-2 must not appear because it did not opt in");
console.log("  [✓] Destination options dynamically re-filter per source VM selection");

// Test 5: renderRoutingPermissions renders toggles with correct checked states and classes
renderRoutingPermissions(vms);
const permHtml = elements['routing-permissions-container'].innerHTML;
assert(permHtml.includes('Inter-Project Allowed'));
assert(permHtml.includes('Isolated to Project'));
assert(permHtml.includes('after:absolute'), "Knob must include after:absolute for proper Tailwind sliding");
console.log("  [✓] renderRoutingPermissions renders status badges and styled toggles");

// Test 6: renderTable renders toggles with after:absolute
renderTable(vms);
const tableHtml = elements['vm-table-body'].innerHTML;
assert(tableHtml.includes('Cross-Project'));
assert(tableHtml.includes('after:absolute'));
console.log("  [✓] renderTable renders Cross-Project toggle with absolute positioning");

// Test 7: Empty VMs array renders fallback messages
renderRoutingPermissions([]);
assert(elements['routing-permissions-container'].innerHTML.includes('No MicroVMs available.'));
console.log("  [✓] renderRoutingPermissions renders empty state fallback cleanly");

// Test 8: Zero running VMs renders selector placeholders
updateLinkSelectors([]);
assert(elements['link-vm1'].innerHTML.includes('No running VMs available'));
assert(elements['link-vm2'].innerHTML.includes('Select Source VM first'));
console.log("  [✓] updateLinkSelectors and updateDestinationSelector handle empty lists gracefully");

// Test 9: Terminating or stopped VMs are excluded from link selectors
const mixedStatusVMs = [
    { id: 10, uuid: 'vm-run', name: 'Running VM', ip: '172.16.0.2', status: 'running', project: 'proj-1', allow_interproject_routing: true },
    { id: 11, uuid: 'vm-term', name: 'Terminating VM', ip: '172.16.0.6', status: 'terminating', project: 'proj-1', allow_interproject_routing: true },
    { id: 12, uuid: 'vm-stop', name: 'Stopped VM', ip: '172.16.0.10', status: 'stopped', project: 'proj-1', allow_interproject_routing: true }
];
updateLinkSelectors(mixedStatusVMs);
assert(elements['link-vm1'].innerHTML.includes('vm-run'));
assert(!elements['link-vm1'].innerHTML.includes('vm-term'), "Terminating VM must not appear in source selector");
assert(!elements['link-vm1'].innerHTML.includes('vm-stop'), "Stopped VM must not appear in source selector");
console.log("  [✓] updateLinkSelectors strictly excludes non-running (terminating, stopped) VMs");

// Test 10: buildProjectSummaries merges persisted empty projects with dynamic VM projects
const persistedProjects = [
    { name: 'empty-fjord', description: 'Ready for winter workloads', persisted: true },
    { name: 'proj-A', description: 'Primary cluster', persisted: true }
];
const mixedFleet = [
    { id: 0, uuid: 'vm-alpha', name: 'Alpha', ip: '172.16.0.2', status: 'running', project: 'proj-A', vcpus: 1, mem_mib: 512, rootfs_gb: 3, allow_interproject_routing: false },
    { id: 1, uuid: 'vm-beta', name: 'Beta', ip: '172.16.0.6', status: 'hibernated', project: 'proj-B', vcpus: 2, mem_mib: 1024, rootfs_gb: 5, allow_interproject_routing: false },
    { id: 2, uuid: 'vm-alpha-2', name: 'Alpha-2', ip: '172.16.0.10', status: 'stopped', project: 'proj-A', vcpus: 1, mem_mib: 512, rootfs_gb: 3, allow_interproject_routing: false }
];

const summaries = buildProjectSummaries(mixedFleet, persistedProjects);
assert.strictEqual(summaries.length, 3, "Should include empty-fjord, proj-A, and dynamically discovered proj-B");
const fjordSummary = summaries.find(p => p.name === 'empty-fjord');
const projASummary = summaries.find(p => p.name === 'proj-A');
const projBSummary = summaries.find(p => p.name === 'proj-B');
assert(fjordSummary && fjordSummary.total === 0 && fjordSummary.persisted === true);
assert(projASummary && projASummary.total === 2 && projASummary.running === 1 && projASummary.stopped === 1);
assert(projBSummary && projBSummary.total === 1 && projBSummary.hibernated === 1 && projBSummary.persisted === false);
console.log("  [✓] buildProjectSummaries merges empty persisted projects and dynamic VM projects accurately");

// Test 11: renderProjects renders clickable project cards including 0-VM empty projects
renderProjects(mixedFleet, persistedProjects);
const gridHtml = elements['projects-grid'].innerHTML;
assert(gridHtml.includes('href="/projects/empty-fjord"'), "Empty project card must link to /projects/empty-fjord");
assert(gridHtml.includes('Empty Workspace'), "Empty project card must show Empty Workspace cue");
assert(gridHtml.includes('href="/projects/proj-A"'), "proj-A card must link to /projects/proj-A");
assert(gridHtml.includes('2 MicroVMs'), "proj-A card must display 2 MicroVMs count");
assert(gridHtml.includes('href="/projects/proj-B"'), "Dynamically discovered proj-B card must link to /projects/proj-B");
assert.strictEqual(elements['projects-count-badge'].textContent, '3 Projects');
assert.strictEqual(elements['summary-projects-count'].textContent, '3');
assert.strictEqual(elements['summary-vms-count'].textContent, '3');
assert.strictEqual(elements['summary-running-count'].textContent, '1');
assert(!elements['projects-empty-state'].classList.contains('hidden') === false);
console.log("  [✓] renderProjects renders clickable project cards and empty projects cleanly");

// Test 12: renderProjects with zero projects shows empty state
renderProjects([], []);
assert.strictEqual(elements['projects-grid'].innerHTML, '');
assert(!elements['projects-empty-state'].classList.contains('hidden'));
console.log("  [✓] renderProjects displays empty state when user has no projects");

// Test 13: filterVMsByProject & renderProjectPage strictly isolate VMs per project
const scopedA = filterVMsByProject(mixedFleet, 'proj-A');
assert.strictEqual(scopedA.length, 2);
assert(scopedA.every(v => v.project === 'proj-A'));

renderProjectPage(mixedFleet, persistedProjects, 'proj-A');
const projATable = elements['vm-table-body'].innerHTML;
assert(projATable.includes('vm-alpha'), "proj-A page must render vm-alpha");
assert(projATable.includes('vm-alpha-2'), "proj-A page must render vm-alpha-2");
assert(!projATable.includes('vm-beta'), "proj-A page must NEVER render vm-beta from proj-B");
assert(!projATable.includes('proj-B'), "proj-A page must NEVER leak proj-B metadata");
assert.strictEqual(elements['vm-count-badge'].textContent, '2 VMs');
assert.strictEqual(elements['project-name'].value, 'proj-A');
assert.strictEqual(elements['project-page-title'].textContent, 'Project: proj-A');
assert(elements['project-not-found'].classList.contains('hidden'));
assert(!elements['project-workspace'].classList.contains('hidden'));
console.log("  [✓] renderProjectPage strictly scopes VMs to proj-A and excludes proj-B VMs");

// Test 14: Dedicated empty project page shows 0 VMs, empty state, and delete-empty-project button
renderProjectPage(mixedFleet, persistedProjects, 'empty-fjord');
assert.strictEqual(elements['vm-table-body'].innerHTML, '');
assert.strictEqual(elements['vm-count-badge'].textContent, '0 VMs');
assert(!elements['empty-state'].classList.contains('hidden'));
assert(!elements['delete-project-btn'].classList.contains('hidden'));
assert(elements['project-not-found'].classList.contains('hidden'));
assert(!elements['project-workspace'].classList.contains('hidden'));
console.log("  [✓] renderProjectPage renders empty persisted project with 0 VMs and delete control");

// Test 15: Unknown/unowned project route shows not-found notice and 0 VMs
renderProjectPage(mixedFleet, persistedProjects, 'foreign-user-project');
assert.strictEqual(elements['vm-table-body'].innerHTML, '');
assert.strictEqual(elements['vm-count-badge'].textContent, '0 VMs');
assert(!elements['project-not-found'].classList.contains('hidden'));
assert(elements['project-workspace'].classList.contains('hidden'));
console.log("  [✓] renderProjectPage isolates unknown/unowned project routes cleanly");

// Test 16: getActiveProjectFromRoute and escapeHtml handling
window.location.pathname = '/projects/nordic%20alpha';
assert.strictEqual(getActiveProjectFromRoute(), 'nordic alpha');
window.location.pathname = '/dashboard';
assert.strictEqual(getActiveProjectFromRoute(), null);
assert.strictEqual(escapeHtml('<script>"x"</script>'), '&lt;script&gt;&quot;x&quot;&lt;/script&gt;');
console.log("  [✓] getActiveProjectFromRoute and escapeHtml handle encoded routes and XSS safely");

// Test 17: Offline daemon fallback (vms = null) preserves persisted VM counts & hides delete btn
const offlineProjects = [
    { name: 'fjord-prod', description: 'Offline test', persisted: true, vm_count: 3, running_count: 2, hibernated_count: 1, stopped_count: 0, booting_count: 0 },
    { name: 'empty-ice', description: '', persisted: true, vm_count: 0, running_count: 0, hibernated_count: 0, stopped_count: 0, booting_count: 0 }
];
renderProjects(null, offlineProjects);
assert.strictEqual(elements['summary-projects-count'].textContent, '2');
assert.strictEqual(elements['summary-vms-count'].textContent, '3');
assert.strictEqual(elements['summary-running-count'].textContent, '2');
assert(elements['projects-grid'].innerHTML.includes('3 MicroVMs'));
assert(!elements['projects-grid'].innerHTML.includes('fjord-prod" class="alpine-project-card block rounded-2xl p-5 group">\n                <div class="flex items-start justify-between gap-2">\n                    <div class="flex items-center gap-2.5 min-w-0">\n                        <span class="w-8 h-8 rounded-xl bg-sky-100/90 border border-sky-200 text-[#0b4f7c] flex items-center justify-center text-sm font-bold shrink-0">❄️</span>\n                        <h4 class="text-base font-bold text-[#0c2238] truncate group-hover:text-[#0b4f7c] transition-colors">fjord-prod</h4>\n                    </div>\n                    <span class="inline-flex items-center px-2 py-0.5 rounded-full text-[10px] font-semibold bg-sky-100/90 text-[#0b4f7c] border border-sky-200">Empty Workspace</span>'));

renderProjectPage(null, offlineProjects, 'fjord-prod');
assert(elements['delete-project-btn'].classList.contains('hidden'), "Must NOT show Delete Empty Project when daemon is offline and project has persisted VMs");
assert.strictEqual(elements['vm-count-badge'].textContent, '3 VMs (Offline)');
assert(elements['empty-state'].classList.contains('hidden'), "Must hide 'No MicroVMs in this project yet' empty state when persisted project has VMs offline");
assert(elements['project-status-summary'].innerHTML.includes('2 Running'));
console.log("  [✓] Offline daemon fallback preserves persisted VM counts and protects non-empty projects");

// Test 18: Unknown project hides status summary & whitespace VM project normalizes to default
renderProjectPage(mixedFleet, persistedProjects, 'unknown-tenant-proj');
assert(elements['project-status-summary'].classList.contains('hidden'));
assert.strictEqual(elements['project-status-summary'].innerHTML, '');
const wsVMs = [{ id: 9, uuid: 'vm-ws', name: 'WS', ip: '172.16.0.14', status: 'running', project: '   ' }];
assert.strictEqual(filterVMsByProject(wsVMs, 'default').length, 1);
console.log("  [✓] Unknown project hides status summary and whitespace project normalizes to default");

// Test 19: escapeJsString and isValidProjectName guard attribute injection and invalid names
const escapedAttr = escapeJsString("vm-1');alert(1);//");
assert(!escapedAttr.includes("')"), "escapeJsString must escape single quotes cleanly");
assert(escapedAttr.includes("\\&#39;"));
assert(isValidProjectName('nordic alpha'));
assert(!isValidProjectName('../escape'));
assert(!isValidProjectName('bad/name'));
console.log("  [✓] escapeJsString and isValidProjectName prevent injection and traversal");

// Test 20: create-missing-project-btn is hidden on invalid project route names and shown on valid missing ones
renderProjectPage(mixedFleet, persistedProjects, 'bad!project');
assert(elements['create-missing-project-btn'].classList.contains('hidden'));
renderProjectPage(mixedFleet, persistedProjects, 'valid-new-proj');
assert(!elements['create-missing-project-btn'].classList.contains('hidden'));
console.log("  [✓] create-missing-project-btn hides on invalid project names and shows on valid missing ones");

// Test 21: Whitespace project names normalize to 'default' consistently across table, selectors, volumes, and routing
const wsPeerVMs = [
    { id: 20, uuid: 'vm-ws-1', name: 'WS-1', ip: '172.16.0.2', status: 'running', project: '   ', allow_interproject_routing: false },
    { id: 21, uuid: 'vm-def-2', name: 'Def-2', ip: '172.16.0.6', status: 'running', project: 'default', allow_interproject_routing: false }
];
renderTable(wsPeerVMs);
assert(elements['vm-table-body'].innerHTML.includes('Project:</span> default'));
updateLinkSelectors(wsPeerVMs);
elements['link-vm1'].value = 'vm-ws-1';
updateDestinationSelector(wsPeerVMs);
assert(elements['link-vm2'].innerHTML.includes('Same Project (default)'));
assert(elements['link-vm2'].innerHTML.includes('value="vm-def-2"'));
renderVolumes([{ id: 5, name: 'vol1', size_gb: 10, attached_vm_id: -1 }], wsPeerVMs);
assert(elements['volumes-container'].innerHTML.includes('min-w-0 max-w-full'));
assert(elements['volumes-container'].innerHTML.includes('(default • 172.16.0.2)'));
renderRoutingPermissions(wsPeerVMs);
assert(elements['routing-permissions-container'].innerHTML.includes('Project:</span> default'));
console.log("  [✓] Whitespace project names normalize consistently across table, selectors, volumes, and routing");

// Test 24: getActiveProjectFromRoute trims trailing spaces and rejects whitespace-only routes cleanly
window.location.pathname = '/projects/nordic%20alpha%20/';
assert.strictEqual(getActiveProjectFromRoute(), 'nordic alpha');
window.location.pathname = '/projects/%20%20';
const wsRouteProj = getActiveProjectFromRoute();
assert.strictEqual(wsRouteProj, ' ');
renderProjectPage(mixedFleet, persistedProjects, wsRouteProj);
assert(!elements['project-not-found'].classList.contains('hidden'));
assert(elements['create-missing-project-btn'].classList.contains('hidden'));
console.log("  [✓] getActiveProjectFromRoute trims trailing spaces and handles whitespace-only routes safely");

// Test 22, 23, 25: copyFromModal applies alpine-btn-success, fetchData drops stale polls, and updatePorts/updateRouting refresh on network failure
(async () => {
    elements['ssh-modal-input'].value = 'ssh -p 2200 root@192.168.1.10';
    elements['ssh-modal-copy-btn'].textContent = 'Copy';
    await copyFromModal();
    assert.strictEqual(elements['ssh-modal-copy-btn'].textContent, 'Copied!');
    assert(elements['ssh-modal-copy-btn'].classList.contains('alpine-btn-success'));
    console.log("  [✓] copyFromModal applies alpine-btn-success class over primary gradient");

    // Out-of-order fetchData race guard
    let resolveSlowVMs;
    let callCount = 0;
    window.location.pathname = '/dashboard';
    lastRenderState = '';
    global.fetch = (url) => {
        callCount += 1;
        if (callCount <= 4) {
            // Slow initial poll (#1)
            if (url === '/api/vms') {
                return new Promise(resolve => {
                    resolveSlowVMs = () => resolve({ ok: true, status: 200, json: () => Promise.resolve([{ id: 1, uuid: 'vm-stale', name: 'Stale', ip: '172.16.0.2', status: 'running', project: 'stale-proj' }]) });
                });
            }
            return Promise.resolve({ ok: true, status: 200, json: () => Promise.resolve([]) });
        }
        // Fast post-mutation refresh (#2)
        if (url === '/api/vms') {
            return Promise.resolve({ ok: true, status: 200, json: () => Promise.resolve([{ id: 2, uuid: 'vm-fresh', name: 'Fresh', ip: '172.16.0.6', status: 'running', project: 'fresh-proj' }]) });
        }
        if (url === '/api/projects') {
            return Promise.resolve({ ok: true, status: 200, json: () => Promise.resolve([{ name: 'fresh-proj', persisted: true, vm_count: 1, running_count: 1, hibernated_count: 0, stopped_count: 0, booting_count: 0 }]) });
        }
        return Promise.resolve({ ok: true, status: 200, json: () => Promise.resolve([]) });
    };

    const pSlow = fetchData();
    await fetchData(); // Fast refresh completes first
    assert(elements['projects-grid'].innerHTML.includes('fresh-proj'));
    resolveSlowVMs();
    await pSlow; // Stale slow response finishes afterwards and MUST NOT overwrite fresh state
    assert(elements['projects-grid'].innerHTML.includes('fresh-proj'));
    assert(!elements['projects-grid'].innerHTML.includes('stale-proj'), "Stale fetchData must not overwrite newer UI state");
    console.log("  [✓] fetchData drops stale out-of-order poll responses");

    // Test 25: updatePorts and updateRouting trigger fetchData refresh even when PATCH throws
    global.alert = () => {};
    let refreshedAfterError = 0;
    global.fetch = (url, opts) => {
        if (opts && opts.method === 'PATCH') {
            return Promise.reject(new Error('network down'));
        }
        refreshedAfterError += 1;
        return Promise.resolve({ ok: true, status: 200, json: () => Promise.resolve([]) });
    };
    await updatePorts('vm-alpha', true, false, false);
    assert(refreshedAfterError > 0, "updatePorts must refresh UI state after network error");
    refreshedAfterError = 0;
    await updateRouting('vm-alpha', true);
    assert(refreshedAfterError > 0, "updateRouting must refresh UI state after network error");
    console.log("  [✓] updatePorts and updateRouting refresh UI state on network failure");

    // Test 26: Project names matching Object.prototype properties (constructor, toString) do not crash selectors
    const protoVMs = [
        { id: 30, uuid: 'vm-proto-1', name: 'Proto-1', ip: '172.16.0.2', status: 'running', project: 'constructor', allow_interproject_routing: true },
        { id: 31, uuid: 'vm-proto-2', name: 'Proto-2', ip: '172.16.0.6', status: 'running', project: 'toString', allow_interproject_routing: true }
    ];
    updateLinkSelectors(protoVMs);
    elements['link-vm1'].value = 'vm-proto-1';
    updateDestinationSelector(protoVMs);
    assert(elements['link-vm1'].innerHTML.includes('Project: constructor'));
    assert(elements['link-vm2'].innerHTML.includes('Inter-Project Authorized (toString)'));
    assert(elements['link-vm2'].innerHTML.includes('value="vm-proto-2"'));
    console.log("  [✓] updateLinkSelectors and updateDestinationSelector handle Object.prototype project names safely");

    // Test 27: fetchData preserves cachedProjects if /api/projects transiently returns 500
    window.location.pathname = '/dashboard';
    global.fetch = (url) => {
        if (url === '/api/projects') {
            return Promise.resolve({ ok: true, status: 200, json: () => Promise.resolve([{ name: 'preserved-empty', description: '', persisted: true }]) });
        }
        return Promise.resolve({ ok: true, status: 200, json: () => Promise.resolve([]) });
    };
    await fetchData();
    assert(elements['projects-grid'].innerHTML.includes('preserved-empty'));

    global.fetch = (url) => {
        if (url === '/api/projects') {
            return Promise.resolve({ ok: false, status: 500, json: () => Promise.resolve({ error: 'db busy' }) });
        }
        return Promise.resolve({ ok: true, status: 200, json: () => Promise.resolve([{ id: 99, uuid: 'vm-new', name: 'New', ip: '172.16.0.2', status: 'running', project: 'other-proj' }]) });
    };
    await fetchData();
    assert(elements['projects-grid'].innerHTML.includes('preserved-empty'), "Transient /api/projects 500 must preserve cachedProjects");
    assert(elements['projects-grid'].innerHTML.includes('other-proj'));
    console.log("  [✓] fetchData preserves cachedProjects on transient /api/projects error");

    // Test 28: renderTable shows >_ Console button only when ssh_exposed is true
    renderTable([
        { id: 0, uuid: 'vm-ssh1', name: 'ConsoleVM', ip: '172.16.0.2', host_ip: '172.16.0.1', status: 'running', ssh_exposed: true, project: 'default' },
        { id: 1, uuid: 'vm-nossh', name: 'NoSshVM', ip: '172.16.0.6', host_ip: '172.16.0.5', status: 'hibernated', ssh_exposed: false, project: 'default' }
    ]);
    assert(elements['vm-table-body'].innerHTML.includes("openConsole('vm-ssh1', 'ConsoleVM')"), "Console button must render when ssh_exposed is true");
    assert(!elements['vm-table-body'].innerHTML.includes("openConsole('vm-nossh'"), "Console button must not render when ssh_exposed is false");
    console.log("  [✓] renderTable renders >_ Console button when ssh_exposed is enabled");

    // Test 29: public domain auto-detection from cryospawn.zibai.space renders https://cryo-<hex>.zibai.space
    window.location.hostname = 'cryospawn.zibai.space';
    renderTable([
        { id: 0, uuid: 'vm-01ec6554', name: 'WebVM', ip: '172.16.0.2', host_ip: '172.16.0.1', status: 'running', http_exposed: true, project: 'default' }
    ]);
    assert(elements['vm-table-body'].innerHTML.includes('https://cryo-01ec6554.zibai.space'), "Public domain link must render when hosted under app.<domain>");
    window.location.hostname = '';
    console.log("  [✓] renderTable renders public domain HTTPS link automatically");

    console.log("[✓] ALL FRONTEND DOM TESTS PASSED!");
    process.exit(0);
})().catch(err => {
    console.error(err);
    process.exit(1);
});
