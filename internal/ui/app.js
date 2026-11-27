const API_URL = '/api/vms';
const LINKS_API_URL = '/api/links';
const VOLUMES_API_URL = '/api/volumes';
const PROJECTS_API_URL = '/api/projects';

const vmTableBody = document.getElementById('vm-table-body');
const emptyState = document.getElementById('empty-state');
const spawnForm = document.getElementById('spawn-form');
const spawnBtn = document.getElementById('spawn-btn');
const vmCountBadge = document.getElementById('vm-count-badge');

const projectsGrid = document.getElementById('projects-grid');
const projectsEmptyState = document.getElementById('projects-empty-state');
const projectsCountBadge = document.getElementById('projects-count-badge');
const createProjectForm = document.getElementById('create-project-form');

const linkForm = document.getElementById('link-form');
const linkVm1 = document.getElementById('link-vm1');
const linkVm2 = document.getElementById('link-vm2');
const linksContainer = document.getElementById('links-container');

const volumeForm = document.getElementById('volume-form');
const volumesContainer = document.getElementById('volumes-container');

let cachedVMs = null;
let cachedVolumes = [];
let cachedProjects = [];
let configuredPublicDomain = '';
let lastRenderState = "";
let fetchSeq = 0;
let latestAppliedSeq = 0;

function getPublicDomain() {
    if (configuredPublicDomain) return configuredPublicDomain;
    const host = (typeof window !== 'undefined' && window.location && window.location.hostname)
        ? window.location.hostname
        : '';
    const match = host.match(/^(?:cryospawn|app|admin)\.([a-zA-Z0-9.-]+\.[a-zA-Z]{2,})$/);
    return match ? match[1] : '';
}

function escapeHtml(str) {
    if (str === null || str === undefined) return '';
    return String(str)
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;')
        .replace(/'/g, '&#39;');
}

function escapeJsString(str) {
    if (str === null || str === undefined) return '';
    const escaped = String(str)
        .replace(/\\/g, '\\\\')
        .replace(/'/g, "\\'")
        .replace(/"/g, '\\"')
        .replace(/\n/g, '\\n')
        .replace(/\r/g, '\\r');
    return escapeHtml(escaped);
}

function normalizeProjectName(raw) {
    return (typeof raw === 'string' ? raw.trim() : '') || 'default';
}

function isValidProjectName(name) {
    if (typeof name !== 'string') return false;
    const stripped = name.trim();
    if (!stripped || stripped.includes('..') || stripped.includes('/') || stripped.includes('\\')) {
        return false;
    }
    return /^[A-Za-z0-9][A-Za-z0-9._ -]{0,63}$/.test(stripped);
}

function toggleMobileNav() {
    const drawer = document.getElementById('mobile-nav-drawer');
    if (drawer) {
        if (drawer.classList.contains('hidden')) {
            drawer.classList.remove('hidden');
        } else {
            drawer.classList.add('hidden');
        }
    }
}

function getActiveProjectFromRoute() {
    const pathname = (typeof window !== 'undefined' && window.location && window.location.pathname)
        ? window.location.pathname
        : '';
    const match = pathname.match(/^\/projects\/([^/]+)\/?$/);
    if (match && match[1]) {
        try {
            return decodeURIComponent(match[1]).trim() || ' ';
        } catch (e) {
            return match[1].trim() || ' ';
        }
    }
    return null;
}

function filterVMsByProject(vms, projectName) {
    if (!Array.isArray(vms)) return [];
    if (projectName === null || projectName === undefined) return vms;
    return vms.filter(vm => normalizeProjectName(vm && vm.project) === projectName);
}

function buildProjectSummaries(vms, persistedProjects) {
    const map = new Map();
    const hasLiveVMs = Array.isArray(vms);

    if (Array.isArray(persistedProjects)) {
        persistedProjects.forEach(proj => {
            if (!proj || !proj.name) return;
            map.set(proj.name, {
                name: proj.name,
                description: proj.description || '',
                created_at: proj.created_at || null,
                persisted: proj.persisted !== false,
                total: hasLiveVMs ? 0 : (Number(proj.vm_count) || 0),
                running: hasLiveVMs ? 0 : (Number(proj.running_count) || 0),
                hibernated: hasLiveVMs ? 0 : (Number(proj.hibernated_count) || 0),
                stopped: hasLiveVMs ? 0 : (Number(proj.stopped_count) || 0),
                booting: hasLiveVMs ? 0 : (Number(proj.booting_count) || 0),
                vms: []
            });
        });
    }

    if (hasLiveVMs) {
        vms.forEach(vm => {
            const projName = normalizeProjectName(vm && vm.project);
            if (!map.has(projName)) {
                map.set(projName, {
                    name: projName,
                    description: '',
                    created_at: null,
                    persisted: false,
                    total: 0,
                    running: 0,
                    hibernated: 0,
                    stopped: 0,
                    booting: 0,
                    vms: []
                });
            }
            const entry = map.get(projName);
            entry.total += 1;
            entry.vms.push(vm);
            if (vm.status === 'running') entry.running += 1;
            else if (vm.status === 'hibernated') entry.hibernated += 1;
            else if (vm.status === 'stopped') entry.stopped += 1;
            else entry.booting += 1;
        });
    }

    return Array.from(map.values()).sort((a, b) => {
        if (a.name === 'default' && b.name !== 'default') return -1;
        if (b.name === 'default' && a.name !== 'default') return 1;
        return a.name.toLowerCase().localeCompare(b.name.toLowerCase());
    });
}

function renderProjects(vms, persistedProjects) {
    const summaries = buildProjectSummaries(vms, persistedProjects);

    const summaryProjectsCount = document.getElementById('summary-projects-count');
    const summaryVMsCount = document.getElementById('summary-vms-count');
    const summaryRunningCount = document.getElementById('summary-running-count');

    const hasLiveVMs = Array.isArray(vms);
    const totalVMs = hasLiveVMs
        ? vms.length
        : summaries.reduce((acc, p) => acc + p.total, 0);
    const runningVMs = hasLiveVMs
        ? vms.filter(v => v.status === 'running').length
        : summaries.reduce((acc, p) => acc + p.running, 0);

    if (summaryProjectsCount) summaryProjectsCount.textContent = String(summaries.length);
    if (summaryVMsCount) summaryVMsCount.textContent = String(totalVMs);
    if (summaryRunningCount) summaryRunningCount.textContent = String(runningVMs);
    if (projectsCountBadge) {
        projectsCountBadge.textContent = `${summaries.length} Project${summaries.length === 1 ? '' : 's'}`;
    }

    const datalist = document.getElementById('project-options');
    if (datalist) {
        datalist.innerHTML = summaries.map(p => `<option value="${escapeHtml(p.name)}"></option>`).join('');
    }

    if (!projectsGrid) return;

    if (summaries.length === 0) {
        projectsGrid.innerHTML = '';
        if (projectsEmptyState) projectsEmptyState.classList.remove('hidden');
        return;
    }

    if (projectsEmptyState) projectsEmptyState.classList.add('hidden');

    projectsGrid.innerHTML = summaries.map(proj => {
        const href = `/projects/${encodeURIComponent(proj.name)}`;
        const isEmpty = proj.total === 0;
        const cueBadge = isEmpty
            ? `<span class="inline-flex items-center px-2 py-0.5 rounded-full text-[10px] font-semibold bg-sky-100/90 text-[#0b4f7c] border border-sky-200 shrink-0 whitespace-nowrap">Empty Workspace</span>`
            : `<span class="inline-flex items-center px-2 py-0.5 rounded-full text-[10px] font-semibold bg-emerald-50 text-emerald-700 border border-emerald-200 shrink-0 whitespace-nowrap">${proj.total} MicroVM${proj.total === 1 ? '' : 's'}</span>`;

        const statusPills = isEmpty
            ? `<span class="text-[11px] text-[#486581]">Ready for first deployment</span>`
            : [
                proj.running > 0 ? `<span class="inline-flex items-center gap-1 text-[11px] font-medium text-emerald-700 bg-emerald-50 border border-emerald-200 px-2 py-0.5 rounded-full"><span class="w-1.5 h-1.5 rounded-full bg-emerald-500"></span>${proj.running} Running</span>` : '',
                proj.hibernated > 0 ? `<span class="inline-flex items-center gap-1 text-[11px] font-medium text-amber-700 bg-amber-50 border border-amber-200 px-2 py-0.5 rounded-full"><span class="w-1.5 h-1.5 rounded-full bg-amber-500"></span>${proj.hibernated} Hibernated</span>` : '',
                proj.stopped > 0 ? `<span class="inline-flex items-center gap-1 text-[11px] font-medium text-slate-700 bg-slate-100 border border-slate-200 px-2 py-0.5 rounded-full"><span class="w-1.5 h-1.5 rounded-full bg-slate-400"></span>${proj.stopped} Stopped</span>` : '',
                proj.booting > 0 ? `<span class="inline-flex items-center gap-1 text-[11px] font-medium text-[#0b4f7c] bg-sky-50 border border-sky-200 px-2 py-0.5 rounded-full status-booting"><span class="w-1.5 h-1.5 rounded-full bg-[#0e6ba8]"></span>${proj.booting} Active</span>` : ''
            ].filter(Boolean).join(' ');

        const descText = proj.description
            ? `<p class="text-xs text-[#486581] mt-1.5 line-clamp-2">${escapeHtml(proj.description)}</p>`
            : `<p class="text-xs text-[#486581] mt-1.5">${isEmpty ? 'Persisted project workspace with 0 instances.' : 'Isolated MicroVM project boundary.'}</p>`;

        return `
            <a href="${href}" data-project-card="${escapeHtml(proj.name)}" class="alpine-project-card block rounded-2xl p-5 group">
                <div class="flex items-start justify-between gap-2">
                    <div class="flex items-center gap-2.5 min-w-0">
                        <span class="w-8 h-8 rounded-xl bg-sky-100/90 border border-sky-200 text-[#0b4f7c] flex items-center justify-center text-sm font-bold shrink-0">❄️</span>
                        <h4 class="text-base font-bold text-[#0c2238] truncate group-hover:text-[#0b4f7c] transition-colors">${escapeHtml(proj.name)}</h4>
                    </div>
                    ${cueBadge}
                </div>
                ${descText}
                <div class="mt-4 pt-3 border-t border-sky-200/70 flex flex-wrap items-center justify-between gap-2">
                    <div class="flex flex-wrap items-center gap-1.5">
                        ${statusPills}
                    </div>
                    <span class="text-xs font-semibold text-[#0b4f7c] group-hover:translate-x-0.5 transition-transform whitespace-nowrap">Open →</span>
                </div>
            </a>
        `;
    }).join('');
}

function renderProjectPage(vms, persistedProjects, activeProject) {
    const targetName = activeProject || getActiveProjectFromRoute();
    if (!targetName) return;

    const hasLiveVMs = Array.isArray(vms);
    const projectNameInput = document.getElementById('project-name');
    if (projectNameInput) {
        projectNameInput.value = targetName;
    }

    const titleEl = document.getElementById('project-page-title');
    if (titleEl) {
        titleEl.textContent = `Project: ${targetName}`;
    }

    const summaries = buildProjectSummaries(vms, persistedProjects);
    const projectSummary = summaries.find(p => p.name === targetName) || null;
    const scopedVMs = hasLiveVMs ? filterVMsByProject(vms, targetName) : [];

    const descEl = document.getElementById('project-description');
    if (descEl) {
        descEl.textContent = (projectSummary && projectSummary.description)
            ? projectSummary.description
            : `Dedicated workspace displaying strictly MicroVMs inside "${targetName}".`;
    }

    const statusSummaryEl = document.getElementById('project-status-summary');
    const notFoundEl = document.getElementById('project-not-found');
    const workspaceEl = document.getElementById('project-workspace');
    const deleteProjectBtn = document.getElementById('delete-project-btn');
    const createMissingBtn = document.getElementById('create-missing-project-btn');

    if (!projectSummary) {
        if (statusSummaryEl) {
            statusSummaryEl.innerHTML = '';
            statusSummaryEl.classList.add('hidden');
        }
        if (notFoundEl) notFoundEl.classList.remove('hidden');
        if (createMissingBtn) {
            if (isValidProjectName(targetName)) {
                createMissingBtn.classList.remove('hidden');
            } else {
                createMissingBtn.classList.add('hidden');
            }
        }
        if (workspaceEl) workspaceEl.classList.add('hidden');
        if (deleteProjectBtn) deleteProjectBtn.classList.add('hidden');
    } else {
        if (statusSummaryEl) {
            statusSummaryEl.classList.remove('hidden');
            statusSummaryEl.innerHTML = `
                <span class="alpine-card px-3 py-1.5 rounded-xl text-xs font-semibold text-emerald-700">${projectSummary.running} Running</span>
                <span class="alpine-card px-3 py-1.5 rounded-xl text-xs font-semibold text-amber-700">${projectSummary.hibernated} Hibernated</span>
                <span class="alpine-card px-3 py-1.5 rounded-xl text-xs font-semibold text-slate-700">${projectSummary.stopped} Stopped</span>
            `;
        }
        if (notFoundEl) notFoundEl.classList.add('hidden');
        if (workspaceEl) workspaceEl.classList.remove('hidden');
        if (deleteProjectBtn) {
            if (hasLiveVMs && projectSummary.total === 0 && projectSummary.persisted) {
                deleteProjectBtn.classList.remove('hidden');
            } else {
                deleteProjectBtn.classList.add('hidden');
            }
        }
    }

    renderTable(scopedVMs);
    if (!hasLiveVMs && projectSummary && projectSummary.total > 0) {
        if (vmCountBadge) {
            vmCountBadge.textContent = `${projectSummary.total} VM${projectSummary.total === 1 ? '' : 's'} (Offline)`;
        }
        if (emptyState) {
            emptyState.classList.add('hidden');
        }
    }
}

async function fetchData() {
    const seq = ++fetchSeq;
    try {
        const [vmsRes, linksRes, volsRes, projsRes] = await Promise.all([
            fetch(API_URL),
            fetch(LINKS_API_URL),
            fetch(VOLUMES_API_URL),
            fetch(PROJECTS_API_URL)
        ]);

        if (seq < latestAppliedSeq) return;

        if (
            vmsRes.status === 401 ||
            linksRes.status === 401 ||
            volsRes.status === 401 ||
            (projsRes && projsRes.status === 401)
        ) {
            window.location.href = '/login';
            return;
        }

        const persistedProjects = (projsRes && projsRes.ok)
            ? await projsRes.json().catch(() => cachedProjects)
            : cachedProjects;
        if (seq < latestAppliedSeq) return;

        const daemonBanner = document.getElementById('daemon-status-banner');
        const activeProject = getActiveProjectFromRoute();

        if (!vmsRes.ok || !linksRes.ok || !volsRes.ok) {
            latestAppliedSeq = seq;
            cachedProjects = persistedProjects;
            if (daemonBanner) daemonBanner.classList.remove('hidden');
            const offlineState = JSON.stringify({ offline: true, persistedProjects, activeProject, cachedVMs });
            if (offlineState !== lastRenderState) {
                if (activeProject) {
                    renderProjectPage(cachedVMs, persistedProjects, activeProject);
                } else {
                    renderProjects(cachedVMs, persistedProjects);
                }
                lastRenderState = offlineState;
            }
            return;
        }

        const [vms, links, volumes] = await Promise.all([
            vmsRes.json(),
            linksRes.json(),
            volsRes.json()
        ]);
        if (seq < latestAppliedSeq) return;
        latestAppliedSeq = seq;

        if (daemonBanner) daemonBanner.classList.add('hidden');

        cachedProjects = persistedProjects;
        cachedVMs = vms;
        cachedVolumes = volumes;

        const currentState = JSON.stringify({ vms, links, volumes, persistedProjects, activeProject });
        if (currentState !== lastRenderState) {
            if (activeProject) {
                renderProjectPage(vms, persistedProjects, activeProject);
            } else {
                renderProjects(vms, persistedProjects);
                renderTable(vms);
                updateLinkSelectors(vms);
                renderLinks(links, vms);
                renderVolumes(volumes, vms);
                renderRoutingPermissions(vms);
            }
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
        const vmUuid = vm.uuid || '';
        const hex = vmUuid.startsWith('vm-') ? vmUuid.substring(3) : vmUuid;
        const safeUuid = escapeHtml(vmUuid);
        const safeJsUuid = escapeJsString(vmUuid);
        const safeJsName = escapeJsString(vm.name || ('VM ' + vm.id));
        const safeJsLanIp = escapeJsString(vm.host_lan_ip || '');
        const safeHostPort = parseInt(vm.host_port, 10) || 0;
        const consoleBtn = vm.ssh_exposed ? `
            <button onclick="openConsole('${safeJsUuid}', '${safeJsName}')" class="text-xs font-semibold text-sky-900 bg-sky-100 hover:bg-sky-200 px-3 py-1.5 rounded-lg transition-colors border border-sky-300">
                &gt;_ Console
            </button>
        ` : '';

        if (vm.status === 'running') {
            statusBadge = `<span class="inline-flex items-center px-2.5 py-0.5 rounded-full text-[11px] font-semibold bg-emerald-50 text-emerald-700 border border-emerald-200"><span class="w-1.5 h-1.5 rounded-full bg-emerald-500 mr-1.5"></span>Running</span>`;

            actionButtons = consoleBtn;
            if (vm.ssh_exposed) {
                actionButtons += `
                    <button onclick="copySSH(this, '${safeJsUuid}', ${safeHostPort}, '${safeJsLanIp}')" class="text-xs font-semibold text-blue-600 bg-blue-50 hover:bg-blue-100 px-3 py-1.5 rounded-lg transition-colors border border-blue-200">
                        <svg class="w-3 h-3 mr-1 inline" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M8 16H6a2 2 0 01-2-2V6a2 2 0 012-2h8a2 2 0 012 2v2m-6 12h8a2 2 0 002-2v-8a2 2 0 00-2-2h-8a2 2 0 00-2 2v8a2 2 0 002 2z"></path></svg>
                        SSH
                    </button>
                `;
            }
            actionButtons += `
                <button onclick="hibernateVM('${safeJsUuid}')" class="text-xs font-semibold text-amber-700 bg-amber-50 hover:bg-amber-100 px-3 py-1.5 rounded-lg transition-colors border border-amber-200">
                    Hibernate
                </button>
                <button onclick="restartVM('${safeJsUuid}')" class="text-xs font-semibold text-[#1e3a52] bg-white/80 hover:bg-sky-50 px-3 py-1.5 rounded-lg transition-colors border border-sky-200">
                    Restart
                </button>
            `;
        } else if (vm.status === 'hibernated') {
            statusBadge = `<span class="inline-flex items-center px-2.5 py-0.5 rounded-full text-[11px] font-semibold bg-amber-50 text-amber-700 border border-amber-200"><span class="w-1.5 h-1.5 rounded-full bg-amber-500 mr-1.5"></span>Hibernated</span>`;
            actionButtons = consoleBtn + `
                <button onclick="wakeVM('${safeJsUuid}')" class="text-xs font-semibold text-emerald-700 bg-emerald-50 hover:bg-emerald-100 px-3 py-1.5 rounded-lg transition-colors border border-emerald-200">
                    Wake VM
                </button>
            `;
        } else if (vm.status === 'stopped') {
            statusBadge = `<span class="inline-flex items-center px-2.5 py-0.5 rounded-full text-[11px] font-semibold bg-slate-100 text-slate-700 border border-slate-200"><span class="w-1.5 h-1.5 rounded-full bg-slate-500 mr-1.5"></span>Stopped</span>`;
            actionButtons = consoleBtn + `
                <button onclick="wakeVM('${safeJsUuid}')" class="text-xs font-semibold text-emerald-700 bg-emerald-50 hover:bg-emerald-100 px-3 py-1.5 rounded-lg transition-colors border border-emerald-200">
                    Start VM
                </button>
            `;
        } else {
            statusBadge = `<span class="inline-flex items-center px-2.5 py-0.5 rounded-full text-[11px] font-semibold bg-sky-50 text-[#0b4f7c] border border-sky-200 status-booting"><span class="w-1.5 h-1.5 rounded-full bg-[#0e6ba8] mr-1.5"></span>${escapeHtml(vm.status)}</span>`;
            actionButtons = `<span class="text-xs text-[#486581] italic">Processing...</span>`;
        }

        let domains = '';
        const pubDomain = getPublicDomain();
        if (vm.http_exposed) {
            if (pubDomain) {
                domains += `<div class="mt-1"><a href="https://cryo-${escapeHtml(hex)}.${escapeHtml(pubDomain)}" target="_blank" class="text-emerald-700 hover:underline text-[10px] font-semibold">https://cryo-${escapeHtml(hex)}.${escapeHtml(pubDomain)}</a></div>`;
            }
            domains += `<div class="mt-0.5"><a href="http://cryo-${escapeHtml(hex)}.cryo" target="_blank" class="text-[#0b4f7c] hover:underline text-[10px] font-medium">http://cryo-${escapeHtml(hex)}.cryo</a></div>`;
        }
        if (vm.https_exposed) domains += `<div class="mt-0.5"><a href="https://cryo-${escapeHtml(hex)}.cryo" target="_blank" class="text-emerald-700 hover:underline text-[10px] font-medium">https://cryo-${escapeHtml(hex)}.cryo</a></div>`;

        let hostPortInfo = '';
        if (vm.ssh_exposed && safeHostPort > 0) {
            hostPortInfo = `<div class="text-[10px] font-mono text-[#0b4f7c] mt-1">SSH Port: ${safeHostPort}</div>`;
        }

        const toggleUI = `
            <div class="mt-3 space-y-2">
                <label class="flex items-center cursor-pointer">
                    <input type="checkbox" class="sr-only peer" ${vm.ssh_exposed ? 'checked' : ''} onchange="updatePorts('${safeJsUuid}', this.checked, ${!!vm.http_exposed}, ${!!vm.https_exposed})">
                    <div class="w-7 h-4 bg-sky-200/80 peer-focus:outline-none rounded-full peer peer-checked:after:translate-x-full peer-checked:after:border-white after:content-[''] after:absolute after:top-[2px] after:left-[2px] after:bg-white after:border-sky-300 after:border after:rounded-full after:h-3 after:w-3 after:transition-all peer-checked:bg-[#0b4f7c] relative shrink-0"></div>
                    <span class="ml-2 text-[10px] font-semibold text-[#1e3a52] whitespace-nowrap">SSH (22)</span>
                </label>
                <label class="flex items-center cursor-pointer">
                    <input type="checkbox" class="sr-only peer" ${vm.http_exposed ? 'checked' : ''} onchange="updatePorts('${safeJsUuid}', ${!!vm.ssh_exposed}, this.checked, ${!!vm.https_exposed})">
                    <div class="w-7 h-4 bg-sky-200/80 peer-focus:outline-none rounded-full peer peer-checked:after:translate-x-full peer-checked:after:border-white after:content-[''] after:absolute after:top-[2px] after:left-[2px] after:bg-white after:border-sky-300 after:border after:rounded-full after:h-3 after:w-3 after:transition-all peer-checked:bg-emerald-600 relative shrink-0"></div>
                    <span class="ml-2 text-[10px] font-semibold text-[#1e3a52] whitespace-nowrap">HTTP (80)</span>
                </label>
                <label class="flex items-center cursor-pointer">
                    <input type="checkbox" class="sr-only peer" ${vm.https_exposed ? 'checked' : ''} onchange="updatePorts('${safeJsUuid}', ${!!vm.ssh_exposed}, ${!!vm.http_exposed}, this.checked)">
                    <div class="w-7 h-4 bg-sky-200/80 peer-focus:outline-none rounded-full peer peer-checked:after:translate-x-full peer-checked:after:border-white after:content-[''] after:absolute after:top-[2px] after:left-[2px] after:bg-white after:border-sky-300 after:border after:rounded-full after:h-3 after:w-3 after:transition-all peer-checked:bg-emerald-600 relative shrink-0"></div>
                    <span class="ml-2 text-[10px] font-semibold text-[#1e3a52] whitespace-nowrap">HTTPS (443)</span>
                </label>
                <label class="flex items-center cursor-pointer">
                    <input type="checkbox" class="sr-only peer" ${vm.allow_interproject_routing ? 'checked' : ''} onchange="updateRouting('${safeJsUuid}', this.checked)">
                    <div class="w-7 h-4 bg-sky-200/80 peer-focus:outline-none rounded-full peer peer-checked:after:translate-x-full peer-checked:after:border-white after:content-[''] after:absolute after:top-[2px] after:left-[2px] after:bg-white after:border-sky-300 after:border after:rounded-full after:h-3 after:w-3 after:transition-all peer-checked:bg-indigo-600 relative shrink-0"></div>
                    <span class="ml-2 text-[10px] font-semibold text-[#1e3a52] whitespace-nowrap">Cross-Project</span>
                </label>
            </div>
        `;

        return `
        <tr class="hover:bg-white/60 transition-colors">
            <td class="px-6 py-4 align-top">
                <div class="font-semibold text-[#0c2238]">${escapeHtml(vm.name || "VM " + vm.id)}</div>
                <div class="text-[10px] text-[#486581] font-mono mt-0.5">${safeUuid}</div>
                <div class="text-xs text-[#486581] mt-1"><span class="font-semibold text-[#1e3a52]">Project:</span> ${escapeHtml(normalizeProjectName(vm.project))}</div>
            </td>
            <td class="px-6 py-4 align-top">${statusBadge}</td>
            <td class="px-6 py-4 align-top font-mono text-xs text-[#1e3a52]">
                <div>${escapeHtml(vm.ip)}</div>
                <div class="text-[10px] text-[#486581] mt-1">Host: ${escapeHtml(vm.host_ip)}</div>
            </td>
            <td class="px-6 py-4 align-top">
                <div class="text-xs text-[#0c2238] font-semibold">${parseInt(vm.vcpus, 10) || 1} vCPU / ${parseInt(vm.mem_mib, 10) || 512} MB RAM</div>
                <div class="text-[10px] text-[#486581] mt-0.5">${parseInt(vm.rootfs_gb, 10) || 3} GB Disk</div>
                ${hostPortInfo}
                ${toggleUI}
                ${domains}
            </td>
            <td class="px-6 py-4 align-top text-right">
                <div class="flex flex-wrap items-center justify-end gap-1.5">
                    ${actionButtons}
                    <button onclick="deleteVM('${safeJsUuid}')" class="text-xs font-semibold text-rose-700 bg-rose-50 hover:bg-rose-100 px-3 py-1.5 rounded-lg transition-colors border border-rose-200">
                        Delete
                    </button>
                </div>
            </td>
        </tr>
        `;
    }).join('');

    if (vmTableBody) vmTableBody.innerHTML = html;
}

if (createProjectForm) {
    createProjectForm.addEventListener('submit', async (e) => {
        e.preventDefault();
        const nameInput = document.getElementById('new-project-name');
        const descInput = document.getElementById('new-project-desc');
        const btn = document.getElementById('create-project-btn');
        const msg = document.getElementById('project-create-msg');

        const name = nameInput ? nameInput.value.trim() : '';
        const description = descInput ? descInput.value.trim() : '';

        if (!name || !isValidProjectName(name)) {
            if (msg) {
                msg.textContent = "Invalid project name. Use 1-64 characters: letters, numbers, spaces, '.', '_', or '-'.";
                msg.className = "text-xs font-semibold text-rose-700 mt-3";
                msg.classList.remove('hidden');
            }
            return;
        }
        if (btn) { btn.disabled = true; btn.textContent = "Creating..."; }
        if (msg) msg.classList.add('hidden');

        try {
            const res = await fetch(PROJECTS_API_URL, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ name, description })
            });
            const data = await res.json().catch(() => ({}));
            if (!res.ok) {
                if (msg) {
                    msg.textContent = data.error || "Failed to create project.";
                    msg.className = "text-xs font-semibold text-rose-700 mt-3";
                    msg.classList.remove('hidden');
                }
            } else {
                if (nameInput) nameInput.value = '';
                if (descInput) descInput.value = '';
                const projectField = document.getElementById('project-name');
                if (projectField && !projectField.readOnly) {
                    projectField.value = name;
                }
                if (msg) {
                    msg.innerHTML = `Project <strong>${escapeHtml(name)}</strong> created. <a href="/projects/${encodeURIComponent(name)}" class="text-[#0b4f7c] underline ml-1">Open Workspace →</a>`;
                    msg.className = "text-xs font-semibold text-emerald-700 mt-3";
                    msg.classList.remove('hidden');
                }
                lastRenderState = "";
                await fetchData();
            }
        } catch (err) {
            if (msg) {
                msg.textContent = "Network error: " + err.message;
                msg.className = "text-xs font-semibold text-rose-700 mt-3";
                msg.classList.remove('hidden');
            }
        } finally {
            if (btn) { btn.disabled = false; btn.textContent = "+ Create Project"; }
        }
    });
}

async function createCurrentProject() {
    const activeProject = getActiveProjectFromRoute();
    if (!activeProject) return;
    try {
        const res = await fetch(PROJECTS_API_URL, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ name: activeProject, description: '' })
        });
        if (res.ok) {
            lastRenderState = "";
            await fetchData();
        } else {
            const data = await res.json().catch(() => ({}));
            alert("Failed to create project: " + (data.error || "Unknown error"));
        }
    } catch (err) {
        alert("Failed to create project: " + err.message);
    }
}

async function deleteCurrentEmptyProject() {
    const activeProject = getActiveProjectFromRoute();
    if (!activeProject) return;
    if (!confirm(`Delete empty project "${activeProject}"?`)) return;

    try {
        const res = await fetch(`${PROJECTS_API_URL}/${encodeURIComponent(activeProject)}`, {
            method: 'DELETE'
        });
        const data = await res.json().catch(() => ({}));
        if (!res.ok) {
            alert("Failed to delete project: " + (data.error || "Unknown error"));
            return;
        }
        window.location.href = '/dashboard';
    } catch (err) {
        alert("Failed to delete project: " + err.message);
    }
}

if (spawnForm) {
    spawnForm.addEventListener('submit', async (e) => {
        e.preventDefault();
        const activeProject = getActiveProjectFromRoute();
        const projectInput = document.getElementById('project-name');
        const targetProject = activeProject || (projectInput ? projectInput.value.trim() : '') || 'default';
        const feedback = document.getElementById('spawn-feedback');

        if (!isValidProjectName(targetProject)) {
            if (feedback) {
                feedback.textContent = "Invalid project name. Use 1-64 characters: letters, numbers, spaces, '.', '_', or '-'.";
                feedback.className = "text-xs rounded-lg p-3 border bg-rose-50 border-rose-200 text-rose-800";
                feedback.classList.remove('hidden');
            }
            return;
        }

        if (spawnBtn) {
            spawnBtn.disabled = true;
            spawnBtn.textContent = "Initializing...";
        }

        const payload = {
            project: targetProject,
            name: document.getElementById('vm-name').value.trim(),
            vcpus: parseInt(document.getElementById('vcpus').value, 10),
            mem_mib: parseInt(document.getElementById('ram').value, 10),
            expose_ssh: document.getElementById('expose-ssh') ? document.getElementById('expose-ssh').checked : false,
            expose_http: document.getElementById('expose-http') ? document.getElementById('expose-http').checked : false,
            expose_https: document.getElementById('expose-https') ? document.getElementById('expose-https').checked : false,
            allow_interproject_routing: document.getElementById('allow-interproject') ? document.getElementById('allow-interproject').checked : false,
            rootfs_gb: parseInt(document.getElementById('rootfs_gb').value, 10)
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
            const vmNameInput = document.getElementById('vm-name');
            if (vmNameInput) vmNameInput.value = '';
            if (feedback) {
                const safeProj = escapeHtml(targetProject);
                const safeName = escapeHtml(payload.name || 'MicroVM');
                feedback.innerHTML = activeProject
                    ? `Deploying <strong>${safeName}</strong> into <strong>${safeProj}</strong>.`
                    : `Deploying <strong>${safeName}</strong> into <strong>${safeProj}</strong>. <a href="/projects/${encodeURIComponent(targetProject)}" class="text-[#0b4f7c] font-semibold underline ml-1">Open Project →</a>`;
                feedback.className = "text-xs rounded-lg p-3 border bg-emerald-50 border-emerald-200 text-emerald-800";
                feedback.classList.remove('hidden');
            }
            lastRenderState = "";
            fetchData();
        } catch (error) {
            if (feedback) {
                feedback.textContent = "Failed to spawn VM: " + error.message;
                feedback.className = "text-xs rounded-lg p-3 border bg-rose-50 border-rose-200 text-rose-800";
                feedback.classList.remove('hidden');
            } else {
                alert("Failed to spawn VM: " + error.message);
            }
        } finally {
            if (spawnBtn) {
                spawnBtn.disabled = false;
                spawnBtn.textContent = "Deploy MicroVM";
            }
        }
    });
}

function updateLinkSelectors(vms) {
    if (!linkVm1 || !linkVm2) return;
    const runningVMs = vms.filter(v => v.status === 'running');
    const prev1 = linkVm1.value;

    const projects = new Map();
    runningVMs.forEach(vm => {
        const proj = normalizeProjectName(vm.project);
        if (!projects.has(proj)) projects.set(proj, []);
        projects.get(proj).push(vm);
    });

    let optHtml1 = '';
    for (const [proj, projVMs] of projects.entries()) {
        optHtml1 += `<optgroup label="Project: ${escapeHtml(proj)}">`;
        projVMs.forEach(vm => {
            const routingTag = vm.allow_interproject_routing ? ' [Inter-Project: ON]' : '';
            optHtml1 += `<option value="${escapeHtml(vm.uuid)}">${escapeHtml(vm.name || 'VM ' + vm.id)} (${escapeHtml(vm.ip)})${routingTag}</option>`;
        });
        optHtml1 += `</optgroup>`;
    }
    linkVm1.innerHTML = optHtml1 || `<option value="" disabled selected>No running VMs available</option>`;
    if (prev1 && runningVMs.some(v => v.uuid === prev1)) {
        linkVm1.value = prev1;
    }

    updateDestinationSelector(vms);
}

function updateDestinationSelector(vms) {
    if (!linkVm1 || !linkVm2) return;
    const runningVMs = vms.filter(v => v.status === 'running');
    const selectedSrcUuid = linkVm1.value;
    const srcVM = runningVMs.find(v => v.uuid === selectedSrcUuid);
    const prev2 = linkVm2.value;

    if (!srcVM) {
        linkVm2.innerHTML = `<option value="" disabled selected>Select Source VM first</option>`;
        return;
    }

    const srcProject = normalizeProjectName(srcVM.project);
    const sameProjectTargets = [];
    const interProjectTargets = [];

    runningVMs.forEach(targetVM => {
        if (targetVM.uuid === srcVM.uuid) return;
        const sameProj = normalizeProjectName(targetVM.project) === srcProject;
        if (sameProj) {
            sameProjectTargets.push(targetVM);
        } else if (srcVM.allow_interproject_routing && targetVM.allow_interproject_routing) {
            interProjectTargets.push(targetVM);
        }
    });

    let optHtml2 = '';
    if (sameProjectTargets.length > 0) {
        optHtml2 += `<optgroup label="Same Project (${escapeHtml(srcProject)})">`;
        sameProjectTargets.forEach(vm => {
            optHtml2 += `<option value="${escapeHtml(vm.uuid)}">${escapeHtml(vm.name || 'VM ' + vm.id)} (${escapeHtml(vm.ip)})</option>`;
        });
        optHtml2 += `</optgroup>`;
    }

    if (interProjectTargets.length > 0) {
        const interProjects = new Map();
        interProjectTargets.forEach(vm => {
            const p = normalizeProjectName(vm.project);
            if (!interProjects.has(p)) interProjects.set(p, []);
            interProjects.get(p).push(vm);
        });

        for (const [p, pVMs] of interProjects.entries()) {
            optHtml2 += `<optgroup label="Inter-Project Authorized (${escapeHtml(p)})">`;
            pVMs.forEach(vm => {
                optHtml2 += `<option value="${escapeHtml(vm.uuid)}">${escapeHtml(vm.name || 'VM ' + vm.id)} (${escapeHtml(vm.ip)}) [Inter-Project]</option>`;
            });
            optHtml2 += `</optgroup>`;
        }
    }

    if (!optHtml2) {
        if (!srcVM.allow_interproject_routing) {
            optHtml2 = `<option value="" disabled selected>No eligible VMs in project "${escapeHtml(srcProject)}" (enable Inter-Project Routing to bridge other projects)</option>`;
        } else {
            optHtml2 = `<option value="" disabled selected>No eligible destination VMs available</option>`;
        }
    }

    linkVm2.innerHTML = optHtml2;
    const allEligible = [...sameProjectTargets, ...interProjectTargets];
    if (prev2 && allEligible.some(v => v.uuid === prev2)) {
        linkVm2.value = prev2;
    }
}

if (linkVm1) {
    linkVm1.addEventListener('change', () => updateDestinationSelector(cachedVMs || []));
}

function renderLinks(links, vms) {
    if (!linksContainer) return;
    if (links.length === 0) {
        linksContainer.innerHTML = `<p class="text-xs text-[#486581] italic">No custom routes active. VMs are currently isolated.</p>`;
        return;
    }

    const getName = (uuid) => {
        const vm = vms.find(v => v.uuid === uuid);
        return vm ? (vm.name || 'VM ' + vm.id) : uuid;
    };

    linksContainer.innerHTML = links.map(l => `
        <div class="alpine-subcard flex flex-wrap items-center justify-between gap-2 p-2.5 rounded-xl text-sm">
            <div class="flex flex-wrap items-center gap-2 text-[#1e3a52]">
                <span class="font-semibold text-xs px-2.5 py-1 bg-white border border-sky-200 rounded-lg">${escapeHtml(getName(l.vm1_uuid))}</span>
                <span class="text-[#0b4f7c] text-xs font-bold">⟷</span>
                <span class="font-semibold text-xs px-2.5 py-1 bg-white border border-sky-200 rounded-lg">${escapeHtml(getName(l.vm2_uuid))}</span>
            </div>
            <button onclick="unlinkVMs('${escapeJsString(l.vm1_uuid)}', '${escapeJsString(l.vm2_uuid)}')" class="text-xs font-semibold text-rose-700 hover:text-rose-800 hover:bg-rose-50 px-2.5 py-1 rounded-lg transition-colors">Sever Link</button>
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

        const btn = document.getElementById('link-btn');
        if (btn) { btn.disabled = true; btn.textContent = "Bridging..."; }

        try {
            const res = await fetch(LINKS_API_URL, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ vm1_id: vm1_uuid, vm2_id: vm2_uuid })
            });
            const data = await res.json().catch(() => ({}));
            if (!res.ok) {
                alert("Failed to bridge VMs: " + (data.error || "Unknown error"));
            } else {
                lastRenderState = "";
                fetchData();
            }
        } catch (err) {
            alert("Failed to bridge VMs: " + err.message);
        } finally {
            if (btn) { btn.disabled = false; btn.textContent = "Bridge Link"; }
        }
    });
}

function renderVolumes(volumes, vms) {
    if (!volumesContainer) return;
    if (!volumes || volumes.length === 0) {
        volumesContainer.innerHTML = `<p class="text-xs text-[#486581] italic">No volumes created yet.</p>`;
        return;
    }

    const prevSelections = {};
    volumes.forEach(vol => {
        const el = document.getElementById(`attach-select-${vol.id}`);
        if (el && el.value) prevSelections[vol.id] = el.value;
    });

    const hasVMs = vms.length > 0;
    const vmOptions = hasVMs
        ? vms.map(v => `<option value="${escapeHtml(v.uuid)}">${escapeHtml(v.name || "VM " + v.id)} (${escapeHtml(normalizeProjectName(v.project))} • ${escapeHtml(v.ip)})</option>`).join('')
        : `<option value="" disabled selected>No VMs available</option>`;

    volumesContainer.innerHTML = volumes.map(vol => {
        const volId = parseInt(vol.id, 10);
        const attached = vol.attached_vm_id !== -1;
        const attachedText = (vol.attached_vm_name || vol.attached_vm_uuid)
            ? `Attached to: ${vol.attached_vm_name || vol.attached_vm_uuid}`
            : 'In Use';

        const statusBadge = attached
            ? `<span class="inline-flex items-center px-2 py-0.5 rounded-full text-[10px] font-semibold bg-emerald-50 text-emerald-700 border border-emerald-200">${escapeHtml(attachedText)}</span>`
            : `<span class="inline-flex items-center px-2 py-0.5 rounded-full text-[10px] font-semibold bg-sky-50 text-[#0b4f7c] border border-sky-200">Available</span>`;

        let actionHtml = attached ? `
            <button onclick="detachVolume(${volId})" class="text-xs font-semibold bg-amber-50 text-amber-700 hover:bg-amber-100 border border-amber-200 px-3 py-1.5 rounded-lg transition-colors">Detach</button>
        ` : `
            <div class="flex flex-wrap items-center gap-2 min-w-0 max-w-full">
                <select id="attach-select-${volId}" class="alpine-input text-xs rounded-lg px-2.5 py-1.5 min-w-0 max-w-full" ${hasVMs ? '' : 'disabled'}>${vmOptions}</select>
                <button onclick="attachVolume(${volId})" class="alpine-btn-primary text-xs font-semibold px-3 py-1.5 rounded-lg shrink-0" ${hasVMs ? '' : 'disabled'}>Attach</button>
            </div>
        `;

        return `
            <div class="alpine-subcard flex flex-wrap items-center justify-between gap-3 p-3.5 rounded-xl">
                <div class="min-w-0">
                    <div class="flex flex-wrap items-center gap-2 min-w-0">
                        <span class="font-bold text-sm text-[#0c2238] break-all">${escapeHtml(vol.name)}</span>
                        <span class="text-xs font-semibold text-[#0b4f7c] bg-white border border-sky-200 px-2 py-0.5 rounded-md">${parseInt(vol.size_gb, 10) || 0} GB</span>
                        ${statusBadge}
                    </div>
                </div>
                <div class="flex flex-wrap items-center gap-2 min-w-0 max-w-full">
                    ${actionHtml}
                    <button onclick="deleteVolume(${volId})" class="text-xs font-semibold text-rose-700 bg-rose-50 hover:bg-rose-100 border border-rose-200 px-3 py-1.5 rounded-lg transition-colors">Delete</button>
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
            const data = await res.json().catch(() => ({}));
            if (res.ok) {
                document.getElementById('vol-name').value = '';
                document.getElementById('vol-size').value = '';
                lastRenderState = "";
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
        const res = await fetch(`${API_URL}/${encodeURIComponent(uuid)}/ports`, {
            method: 'PATCH',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ ssh_exposed: ssh, http_exposed: http, https_exposed: https })
        });
        const data = await res.json().catch(() => ({}));
        if (!res.ok) {
            alert("Failed to update ports: " + (data.error || "Unknown error"));
        }
    } catch (err) {
        alert("Failed to update ports: " + err.message);
    } finally {
        lastRenderState = "";
        fetchData();
    }
}

async function updateRouting(uuid, allow) {
    try {
        const res = await fetch(`${API_URL}/${encodeURIComponent(uuid)}/routing`, {
            method: 'PATCH',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ allow_interproject_routing: allow })
        });
        const data = await res.json().catch(() => ({}));
        if (!res.ok) {
            alert("Failed to update routing permission: " + (data.error || "Unknown error"));
        }
    } catch (err) {
        alert("Failed to update routing permission: " + err.message);
    } finally {
        lastRenderState = "";
        fetchData();
    }
}

function renderRoutingPermissions(vms) {
    const container = document.getElementById('routing-permissions-container');
    if (!container) return;
    if (!vms || vms.length === 0) {
        container.innerHTML = `<p class="text-xs text-[#486581] italic">No MicroVMs available.</p>`;
        return;
    }

    container.innerHTML = vms.map(vm => {
        const isAllowed = !!vm.allow_interproject_routing;
        const statusColor = vm.status === 'running' ? 'bg-emerald-500' : (vm.status === 'hibernated' ? 'bg-amber-500' : 'bg-slate-400');
        const badge = isAllowed
            ? `<span class="inline-flex items-center px-2 py-0.5 rounded-full text-[10px] font-semibold bg-indigo-50 text-indigo-700 border border-indigo-200">Inter-Project Allowed</span>`
            : `<span class="inline-flex items-center px-2 py-0.5 rounded-full text-[10px] font-semibold bg-sky-50 text-[#0b4f7c] border border-sky-200">Isolated to Project</span>`;

        return `
            <div class="alpine-subcard flex flex-wrap items-center justify-between gap-3 p-3.5 rounded-xl">
                <div class="flex items-center space-x-3 min-w-0">
                    <span class="w-2 h-2 rounded-full ${statusColor} shrink-0"></span>
                    <div class="min-w-0">
                        <div class="flex flex-wrap items-center gap-2">
                            <span class="text-sm font-bold text-[#0c2238] break-all">${escapeHtml(vm.name || 'VM ' + vm.id)}</span>
                            <span class="text-xs text-[#486581] font-mono">(${escapeHtml(vm.ip)})</span>
                            ${badge}
                        </div>
                        <div class="text-xs text-[#486581] mt-0.5 break-all">
                            <span class="font-semibold text-[#1e3a52]">Project:</span> ${escapeHtml(normalizeProjectName(vm.project))} &bull;
                            <span class="font-mono text-[11px] text-[#486581]">${escapeHtml(vm.uuid)}</span>
                        </div>
                    </div>
                </div>
                <div class="flex items-center space-x-3">
                    <label class="flex items-center cursor-pointer">
                        <input type="checkbox" class="sr-only peer" ${isAllowed ? 'checked' : ''} onchange="updateRouting('${escapeJsString(vm.uuid)}', this.checked)">
                        <div class="w-9 h-5 bg-sky-200/80 peer-focus:outline-none rounded-full peer peer-checked:after:translate-x-full peer-checked:after:border-white after:content-[''] after:absolute after:top-[2px] after:left-[2px] after:bg-white after:border-sky-300 after:border after:rounded-full after:h-4 after:w-4 after:transition-all peer-checked:bg-indigo-600 relative shrink-0"></div>
                        <span class="ml-2.5 text-xs font-semibold text-[#1e3a52] whitespace-nowrap">Allow Cross-Project</span>
                    </label>
                </div>
            </div>
        `;
    }).join('');
}

async function deleteVM(uuid) {
    if (confirm("Permanently delete this microVM and free disk resources?")) {
        try {
            const res = await fetch(`${API_URL}/${encodeURIComponent(uuid)}`, {method:'DELETE'});
            if (!res.ok) {
                const data = await res.json().catch(() => ({}));
                alert("Failed to delete VM: " + (data.error || "Unknown error"));
            }
            lastRenderState = "";
            fetchData();
        } catch (err) {
            alert("Failed to delete VM: " + err.message);
        }
    }
}

async function restartVM(uuid) {
    if (confirm("Restart this microVM?")) {
        try {
            const res = await fetch(`${API_URL}/${encodeURIComponent(uuid)}/restart`, {method:'POST'});
            if (!res.ok) {
                const data = await res.json().catch(() => ({}));
                alert("Failed to restart VM: " + (data.error || "Unknown error"));
            }
            lastRenderState = "";
            fetchData();
        } catch (err) {
            alert("Failed to restart VM: " + err.message);
        }
    }
}

async function hibernateVM(uuid) {
    try {
        const res = await fetch(`${API_URL}/${encodeURIComponent(uuid)}/hibernate`, {method:'POST'});
        if (!res.ok) {
            const data = await res.json().catch(() => ({}));
            alert("Failed to hibernate VM: " + (data.error || "Unknown error"));
        }
        lastRenderState = "";
        fetchData();
    } catch (err) {
        alert("Failed to hibernate VM: " + err.message);
    }
}

async function wakeVM(uuid) {
    try {
        const res = await fetch(`${API_URL}/${encodeURIComponent(uuid)}/wake`, {method:'POST'});
        if (!res.ok) {
            const data = await res.json().catch(() => ({}));
            alert("Failed to start/wake VM: " + (data.error || "Unknown error"));
        }
        lastRenderState = "";
        fetchData();
    } catch (err) {
        alert("Failed to start/wake VM: " + err.message);
    }
}

async function unlinkVMs(vm1, vm2) {
    try {
        const res = await fetch(`${LINKS_API_URL}/${encodeURIComponent(vm1)}/${encodeURIComponent(vm2)}`, {method:'DELETE'});
        if (!res.ok) {
            const data = await res.json().catch(() => ({}));
            alert("Failed to unlink VMs: " + (data.error || "Unknown error"));
        }
        lastRenderState = "";
        fetchData();
    } catch (err) {
        alert("Failed to unlink VMs: " + err.message);
    }
}

async function deleteVolume(id) {
    if (confirm("Delete this volume?")) {
        try {
            const res = await fetch(`${VOLUMES_API_URL}/${encodeURIComponent(id)}`, {method:'DELETE'});
            const data = await res.json().catch(() => ({}));
            if (!res.ok) {
                alert("Failed to delete volume: " + (data.error || "Unknown error"));
            }
            lastRenderState = "";
            fetchData();
        } catch (err) {
            alert("Failed to delete volume: " + err.message);
        }
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
        try {
            const res = await fetch(`${VOLUMES_API_URL}/${encodeURIComponent(vol_id)}/attach`, {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({vm_id: vm_uuid})
            });
            const data = await res.json().catch(() => ({}));
            if (!res.ok) {
                alert("Failed to attach volume: " + (data.error || "Unknown error"));
            }
            lastRenderState = "";
            fetchData();
        } catch (err) {
            alert("Failed to attach volume: " + err.message);
        }
    }
}

async function detachVolume(vol_id) {
    if (confirm("Detaching will reload the target VM to safely unmount the drive. Proceed?")) {
        try {
            const res = await fetch(`${VOLUMES_API_URL}/${encodeURIComponent(vol_id)}/detach`, {method:'POST'});
            const data = await res.json().catch(() => ({}));
            if (!res.ok) {
                alert("Failed to detach volume: " + (data.error || "Unknown error"));
            }
            lastRenderState = "";
            fetchData();
        } catch (err) {
            alert("Failed to detach volume: " + err.message);
        }
    }
}

async function copyTextToClipboard(text) {
    if (navigator.clipboard && window.isSecureContext) {
        try {
            await navigator.clipboard.writeText(text);
            return true;
        } catch (err) {
            console.warn("navigator.clipboard write failed:", err);
        }
    }

    try {
        const textArea = document.createElement("textarea");
        textArea.value = text;
        textArea.style.top = "0";
        textArea.style.left = "0";
        textArea.style.position = "fixed";
        textArea.style.opacity = "0";
        textArea.setAttribute("readonly", "");
        document.body.appendChild(textArea);
        textArea.focus();
        textArea.select();
        textArea.setSelectionRange(0, 99999);
        const successful = document.execCommand("copy");
        document.body.removeChild(textArea);
        if (successful) return true;
    } catch (err) {
        console.warn("execCommand fallback failed:", err);
    }

    return false;
}

function showSSHModal(cmd) {
    const modal = document.getElementById('ssh-modal');
    const input = document.getElementById('ssh-modal-input');
    if (modal && input) {
        input.value = cmd;
        modal.classList.remove('hidden');
        input.focus();
        input.select();
    } else {
        window.prompt("SSH Command (Ctrl+C / tap to copy):", cmd);
    }
}

function closeSSHModal() {
    const modal = document.getElementById('ssh-modal');
    if (modal) modal.classList.add('hidden');
}

async function copyFromModal() {
    const input = document.getElementById('ssh-modal-input');
    const btn = document.getElementById('ssh-modal-copy-btn');
    if (!input) return;
    const ok = await copyTextToClipboard(input.value);
    if (btn) {
        if (btn.dataset && btn.dataset.copying === '1') return;
        const orig = btn.textContent;
        if (btn.dataset) btn.dataset.copying = '1';
        btn.textContent = ok ? "Copied!" : "Selected";
        btn.classList.add('alpine-btn-success', 'bg-emerald-600');
        setTimeout(() => {
            btn.textContent = orig;
            btn.classList.remove('alpine-btn-success', 'bg-emerald-600');
            if (btn.dataset) delete btn.dataset.copying;
        }, 2000);
    }
}

async function copySSH(btn, uuid, hostPort, hostLanIp) {
    const hex = uuid.startsWith('vm-') ? uuid.substring(3) : uuid;

    let targetHost = "localhost";
    const currentHost = window.location.hostname;
    if (currentHost && currentHost !== "localhost" && currentHost !== "127.0.0.1") {
        targetHost = currentHost;
    } else if (hostLanIp && hostLanIp !== "127.0.0.1") {
        targetHost = hostLanIp;
    }

    let cmd = "";
    if (hostPort && parseInt(hostPort, 10) > 0) {
        cmd = `ssh -p ${parseInt(hostPort, 10)} root@${targetHost}`;
    } else {
        cmd = `ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ProxyCommand="nc -U /tmp/cryo-${hex}_22.sock" root@localhost`;
    }

    const copied = await copyTextToClipboard(cmd);

    if (copied) {
        if (btn.dataset && btn.dataset.copying === '1') return;
        const originalHtml = btn.innerHTML;
        if (btn.dataset) btn.dataset.copying = '1';
        btn.innerHTML = `<svg class="w-3 h-3 mr-1 inline" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M5 13l4 4L19 7"></path></svg> Copied!`;
        btn.classList.replace('text-blue-600', 'text-emerald-700');
        btn.classList.replace('bg-blue-50', 'bg-emerald-50');
        btn.classList.replace('border-blue-200', 'border-emerald-200');
        setTimeout(() => {
            btn.innerHTML = originalHtml;
            btn.classList.replace('text-emerald-700', 'text-blue-600');
            btn.classList.replace('bg-emerald-50', 'bg-blue-50');
            btn.classList.replace('border-emerald-200', 'border-blue-200');
            if (btn.dataset) delete btn.dataset.copying;
        }, 2000);
    } else {
        showSSHModal(cmd);
    }
}

// Pre-fill locked project input on dedicated project route before first poll
const initialProject = getActiveProjectFromRoute();
if (initialProject) {
    const projInput = document.getElementById('project-name');
    if (projInput) projInput.value = initialProject;
    const titleEl = document.getElementById('project-page-title');
    if (titleEl) titleEl.textContent = `Project: ${initialProject}`;
}

const needsLiveData = !!(
    vmTableBody ||
    projectsGrid ||
    linksContainer ||
    volumesContainer ||
    document.getElementById('routing-permissions-container')
);
if (needsLiveData) {
    fetch('/api/config').then(r => r.ok ? r.json() : null).then(cfg => {
        if (cfg && cfg.public_domain) {
            configuredPublicDomain = String(cfg.public_domain).trim();
            lastRenderState = "";
            fetchData();
        }
    }).catch(() => {});
    fetchData();
    setInterval(fetchData, 2000);
}

const rootfsGb = document.getElementById('rootfs_gb');
const rootfsDisplay = document.getElementById('rootfs_display');
if (rootfsGb && rootfsDisplay) {
    rootfsGb.addEventListener('input', (e) => rootfsDisplay.textContent = e.target.value + ' GB');
}

let activeConsoleTerm = null;
let activeConsoleFit = null;
let activeConsoleWs = null;
let activeConsoleUuid = '';

function setConsoleModalStatus(label, tone) {
    const el = document.getElementById('console-modal-status');
    if (!el) return;
    const classes = {
        connecting: 'bg-amber-950/70 text-amber-300 border-amber-700/60',
        connected: 'bg-emerald-950/70 text-emerald-300 border-emerald-700/60',
        disconnected: 'bg-rose-950/70 text-rose-300 border-rose-700/60'
    };
    el.className = `inline-flex items-center gap-1.5 text-[11px] font-semibold px-2 py-0.5 rounded-full border ${classes[tone] || classes.disconnected}`;
    el.textContent = label;
}

function connectConsoleSocket(uuid) {
    if (!activeConsoleTerm || !uuid) return;
    if (activeConsoleWs) {
        try { activeConsoleWs.close(); } catch (_) {}
        activeConsoleWs = null;
    }
    setConsoleModalStatus('Connecting...', 'connecting');
    activeConsoleTerm.writeln(`\x1b[36m[*] Opening console to ${uuid}...\x1b[0m`);

    const proto = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const ws = new WebSocket(`${proto}//${window.location.host}/ws/console/${encodeURIComponent(uuid)}`);
    activeConsoleWs = ws;

    ws.onopen = () => {
        setConsoleModalStatus('Connected', 'connected');
        if (activeConsoleFit) activeConsoleFit.fit();
        ws.send(JSON.stringify({ type: 'resize', cols: activeConsoleTerm.cols, rows: activeConsoleTerm.rows }));
        activeConsoleTerm.focus();
    };
    ws.onmessage = (evt) => {
        if (activeConsoleTerm) activeConsoleTerm.write(evt.data);
    };
    ws.onclose = () => {
        if (activeConsoleWs === ws) {
            setConsoleModalStatus('Disconnected', 'disconnected');
            if (activeConsoleTerm) activeConsoleTerm.writeln('\r\n\x1b[33m[!] Session closed.\x1b[0m');
        }
    };
    ws.onerror = () => {
        setConsoleModalStatus('Error', 'disconnected');
        if (activeConsoleTerm) activeConsoleTerm.writeln('\r\n\x1b[31m[-] WebSocket error.\x1b[0m');
    };
}

function openConsole(uuid, name) {
    const modal = document.getElementById('console-modal');
    const container = document.getElementById('console-terminal');
    if (!modal || !container || typeof Terminal === 'undefined') {
        window.open(`/console/${encodeURIComponent(uuid)}`, '_blank');
        return;
    }

    activeConsoleUuid = uuid;
    const titleEl = document.getElementById('console-modal-title');
    const uuidEl = document.getElementById('console-modal-uuid');
    const popoutEl = document.getElementById('console-modal-popout');
    if (titleEl) titleEl.textContent = name || uuid;
    if (uuidEl) uuidEl.textContent = uuid;
    if (popoutEl) popoutEl.href = `/console/${encodeURIComponent(uuid)}`;

    modal.classList.remove('hidden');

    if (!activeConsoleTerm) {
        activeConsoleTerm = new Terminal({
            cursorBlink: true,
            fontSize: 13,
            fontFamily: 'ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace',
            theme: { background: '#0c1624', foreground: '#e2e8f0', cursor: '#38bdf8' }
        });
        if (typeof FitAddon !== 'undefined' && FitAddon.FitAddon) {
            activeConsoleFit = new FitAddon.FitAddon();
            activeConsoleTerm.loadAddon(activeConsoleFit);
        }
        activeConsoleTerm.open(container);
        activeConsoleTerm.onData((data) => {
            if (activeConsoleWs && activeConsoleWs.readyState === WebSocket.OPEN) {
                activeConsoleWs.send(JSON.stringify({ type: 'input', data }));
            }
        });
        activeConsoleTerm.onResize(({ cols, rows }) => {
            if (activeConsoleWs && activeConsoleWs.readyState === WebSocket.OPEN) {
                activeConsoleWs.send(JSON.stringify({ type: 'resize', cols, rows }));
            }
        });
        if (typeof window !== 'undefined' && typeof window.addEventListener === 'function') {
            window.addEventListener('resize', () => {
                if (activeConsoleFit && !modal.classList.contains('hidden')) {
                    activeConsoleFit.fit();
                }
            });
        }
    } else {
        activeConsoleTerm.reset();
    }

    if (activeConsoleFit) {
        setTimeout(() => activeConsoleFit.fit(), 30);
    }
    connectConsoleSocket(uuid);
}

function reconnectConsole() {
    if (activeConsoleUuid) connectConsoleSocket(activeConsoleUuid);
}

function closeConsoleModal() {
    const modal = document.getElementById('console-modal');
    if (modal) modal.classList.add('hidden');
    if (activeConsoleWs) {
        try { activeConsoleWs.close(); } catch (_) {}
        activeConsoleWs = null;
    }
}

if (typeof document !== 'undefined' && typeof document.addEventListener === 'function') {
    document.addEventListener('keydown', (e) => {
        if (e.key === 'Escape') {
            closeSSHModal();
            closeConsoleModal();
        }
    });
}

