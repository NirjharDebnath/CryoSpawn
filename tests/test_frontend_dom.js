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
        this.classList = {
            classes: new Set(),
            add(c) { this.classes.add(c); },
            remove(c) { this.classes.delete(c); },
            contains(c) { return this.classes.has(c); }
        };
        this.listeners = {};
    }
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
    'allow-interproject': new Element('allow-interproject')
};

global.document = {
    getElementById(id) {
        return elements[id] || null;
    }
};

global.window = {
    location: { href: '' }
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

console.log("[✓] ALL FRONTEND DOM TESTS PASSED!");
process.exit(0);
