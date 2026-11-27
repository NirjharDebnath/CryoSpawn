// Shared layout, API helper, and xterm.js modal for CryoSpawn God-Mode Admin

function esc(s) {
    return String(s ?? '')
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;')
        .replace(/'/g, '&#39;');
}

async function api(url, opts = {}) {
    const res = await fetch(url, opts);
    if (res.status === 401) {
        window.location.href = '/login';
        throw new Error('Unauthorized');
    }
    return res;
}

async function adminLogout() {
    await fetch('/api/admin/logout', { method: 'POST' });
    window.location.href = '/login';
}

function renderAdminShell(activeKey) {
    const navItems = [
        { key: 'overview', href: '/', icon: '⚡', label: 'Overview & Daemon' },
        { key: 'vms', href: '/vms', icon: '🖥️', label: 'Global MicroVMs' },
        { key: 'rootfs', href: '/rootfs', icon: '💿', label: 'RootFS Studio' },
        { key: 'database', href: '/database', icon: '🗄️', label: 'Users & SQLite' },
    ];

    const navHtml = navItems.map(item => {
        const active = item.key === activeKey;
        const cls = active
            ? 'bg-[#0b4f7c] text-white shadow-sm'
            : 'text-[#1e3a52] hover:bg-sky-100/80';
        return `<a href="${item.href}" class="flex items-center gap-2.5 px-3.5 py-2.5 rounded-xl text-sm font-semibold transition-colors ${cls}"><span>${item.icon}</span><span>${item.label}</span></a>`;
    }).join('');

    const sidebar = document.getElementById('admin-sidebar');
    if (sidebar) {
        sidebar.innerHTML = `
            <div class="p-5 border-b border-sky-200/80">
                <div class="flex items-center gap-2.5">
                    <span class="w-9 h-9 rounded-xl bg-[#0b4f7c] text-white flex items-center justify-center font-bold shadow-sm">⚡</span>
                    <div>
                        <div class="text-base font-bold text-[#0c2238] leading-none">CryoSpawn</div>
                        <div class="text-[11px] font-medium text-[#486581] mt-1">God-Mode Controller</div>
                    </div>
                </div>
                <div id="host-priv-badge" class="mt-3 text-[11px] font-semibold text-[#0b4f7c] bg-sky-50 border border-sky-200 rounded-lg px-2.5 py-1.5">Checking privileges...</div>
            </div>
            <nav class="flex-1 p-3 space-y-1">${navHtml}</nav>
            <div class="p-3 border-t border-sky-200/80">
                <button onclick="adminLogout()" class="w-full px-3.5 py-2 rounded-xl text-xs font-semibold text-rose-700 bg-rose-50 hover:bg-rose-100 border border-rose-200 transition-colors">Logout</button>
            </div>
        `;
    }

    const mobileNav = document.getElementById('admin-mobile-nav');
    if (mobileNav) {
        mobileNav.innerHTML = navItems.map(item => {
            const active = item.key === activeKey;
            const cls = active ? 'bg-[#0b4f7c] text-white' : 'bg-white text-[#1e3a52] border border-sky-200';
            return `<a href="${item.href}" class="px-2.5 py-1.5 rounded-lg text-xs font-semibold whitespace-nowrap ${cls}">${item.icon} ${item.label}</a>`;
        }).join('');
    }

    api('/api/admin/me').then(r => r.json()).then(me => {
        const el = document.getElementById('host-priv-badge');
        if (el) {
            el.textContent = `${me.username} • ${me.is_root ? 'ROOT (UID 0)' : 'NON-ROOT'}`;
        }
    }).catch(() => {});
}

let _adminTerm = null;
let _adminFit = null;
let _adminWs = null;

function openAdminTerm(wsPath, title) {
    let modal = document.getElementById('admin-term-modal');
    if (!modal) return;
    document.getElementById('term-title').textContent = title;
    modal.classList.remove('hidden');

    if (!_adminTerm) {
        _adminTerm = new Terminal({
            cursorBlink: true,
            fontSize: 13,
            fontFamily: 'ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace',
            theme: { background: '#0c1624', foreground: '#e2e8f0', cursor: '#38bdf8' }
        });
        _adminFit = new FitAddon.FitAddon();
        _adminTerm.loadAddon(_adminFit);
        _adminTerm.open(document.getElementById('admin-xterm'));
        _adminTerm.onData(d => {
            if (_adminWs && _adminWs.readyState === WebSocket.OPEN) {
                _adminWs.send(JSON.stringify({ type: 'input', data: d }));
            }
        });
        _adminTerm.onResize(({ cols, rows }) => {
            if (_adminWs && _adminWs.readyState === WebSocket.OPEN) {
                _adminWs.send(JSON.stringify({ type: 'resize', cols, rows }));
            }
        });
        window.addEventListener('resize', () => {
            if (_adminFit && !modal.classList.contains('hidden')) _adminFit.fit();
        });
    } else {
        _adminTerm.reset();
    }

    if (_adminWs) {
        try { _adminWs.close(); } catch (_) {}
    }
    const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
    _adminWs = new WebSocket(`${proto}//${location.host}${wsPath}`);
    const st = document.getElementById('term-status');
    st.textContent = 'Connecting...';
    st.className = 'text-[11px] font-semibold px-2.5 py-0.5 rounded-full bg-amber-950/70 text-amber-300 border border-amber-700/60';

    _adminWs.onopen = () => {
        st.textContent = 'Connected';
        st.className = 'text-[11px] font-semibold px-2.5 py-0.5 rounded-full bg-emerald-950/70 text-emerald-300 border border-emerald-700/60';
        _adminFit.fit();
        _adminWs.send(JSON.stringify({ type: 'resize', cols: _adminTerm.cols, rows: _adminTerm.rows }));
        _adminTerm.focus();
    };
    _adminWs.onmessage = (e) => _adminTerm.write(e.data);
    _adminWs.onclose = () => {
        st.textContent = 'Disconnected';
        st.className = 'text-[11px] font-semibold px-2.5 py-0.5 rounded-full bg-rose-950/70 text-rose-300 border border-rose-700/60';
        _adminTerm.writeln('\r\n\x1b[33m[!] Session closed.\x1b[0m');
    };
    _adminWs.onerror = () => {
        st.textContent = 'Error';
    };
}

function closeAdminTerm() {
    const modal = document.getElementById('admin-term-modal');
    if (modal) modal.classList.add('hidden');
    if (_adminWs) {
        try { _adminWs.close(); } catch (_) {}
        _adminWs = null;
    }
}
