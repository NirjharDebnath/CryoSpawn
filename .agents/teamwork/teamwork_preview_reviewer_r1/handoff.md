# CryoSpawn Adversarial Review Round 1 Handoff

> [!WARNING] **Skepticism Disclaimer**
> All C++ core tests, 15 Python Flask auth/project API tests, 19 Node.js mock-DOM tests, inline HTML script syntax checks, and live Flask HTTP E2E checks pass against isolated temporary databases; however, real Chromium/Firefox visual rendering, Tailwind CDN CSS evaluation, and live KVM/Firecracker daemon execution remain unverified because no headless browser engine is installed in this environment.

## 1. What the prior attempt got wrong
1. **Responsive flex overflow clipping on `#create-project-form` (`internal/ui/dashboard.html`)**:
   - **Input/Context**: Viewport at `lg:` (`1024px–1200px`), where sidebar (`256px`) + `lg:grid-cols-3` (`lg:col-span-2` card inner width `~410px`) contains `#create-project-form`.
   - **Expected**: Form inputs and `+ Create Project` button wrap cleanly inside the card without clipping.
   - **Actual**: `#create-project-form` had `flex-col sm:flex-row` without `flex-wrap`, with two `min-w-[160px]` children + `whitespace-nowrap` button (`~492px` > `410px`), causing right-edge clipping inside `section.overflow-hidden`.
   - **Root Cause**: Missing `sm:flex-wrap` and overly wide `min-w-[160px]` on form flex items. Fixed with `sm:flex-wrap` and `min-w-[140px]`.

2. **Mobile SSH modal input flex overflow (`internal/ui/project.html`)**:
   - **Input/Context**: Narrow mobile viewport (`<= 390px`) opening `#ssh-modal` with a long UDS `ssh -o StrictHostKeyChecking=no ...` command.
   - **Expected**: `#ssh-modal-input` shrinks cleanly beside the `Copy` button.
   - **Actual**: `#ssh-modal-input` had `flex-1` without `min-w-0`, allowing intrinsic input width to push the `Copy` button against/past the modal container edge.
   - **Root Cause**: Standard flexbox `min-width: auto` default; fixed by adding `min-w-0` on `#ssh-modal-input` and `shrink-0` on `#ssh-modal-copy-btn`.

3. **Daemon-offline project count loss & false "Delete Empty Project" prompt (`internal/ui/app.js`)**:
   - **Input/Context**: C++ hypervisor daemon unreachable (`GET /api/vms` returns 502) while user owns projects with persisted VMs in SQLite (`GET /api/projects` returns `vm_count > 0`, `running_count`, etc.).
   - **Expected**: Dashboard cards and `/projects/<name>` summary reflect persisted counts from `/api/projects`, and `/projects/<name>` does NOT show `#delete-project-btn`.
   - **Actual**: `cachedVMs` initialized to `[]`; `buildProjectSummaries([], persistedProjects)` ignored `proj.vm_count` / `running_count` / `hibernated_count` / `stopped_count` and showed `"Empty Workspace"`, `0 Total VMs`, and exposed `#delete-project-btn`.
   - **Root Cause**: `buildProjectSummaries` treated `[]` (no VMs) identically to daemon-unreachable (`vms === null`). Fixed by initializing `cachedVMs = null`, falling back to server-side counts in `buildProjectSummaries(null, persistedProjects)`, gating `#delete-project-btn` on `hasLiveVMs && projectSummary.total === 0`, and caching offline render state to avoid 2s DOM thrashing.

4. **Malformed non-dict JSON HTTP 500s, `/index.html` legacy slate page exposure, `POST /api/vms` project validation gap, and `delete_project` normalization mismatch (`frontend/app.py`)**:
   - **Input/Context**:
     - POSTing `[]` or `"str"` JSON to `/api/auth/login`, `/api/auth/register`, `/api/auth/change-password`, or `/api/projects`.
     - Authenticated user navigating to `/index.html`.
     - `POST /api/vms` via `proxy_api` with `project: "../escape"` or `bad/name`.
     - `DELETE /api/projects/default` when a VM row has whitespace `project_name = "   "`.
   - **Expected**: `400`/`401` JSON errors (never `500`), `/index.html` redirecting to `/dashboard` like `/`, invalid `project` names rejected with `400`, and `'default'` deletion blocked when whitespace-project VMs exist.
   - **Actual**: Non-dict JSON raised `AttributeError` (HTTP 500); `/index.html` returned `200` with old un-themed slate HTML; `POST /api/vms` forwarded `../escape` to C++ backend; `delete_project` matched `project_name = 'default'` instead of normalized `TRIM(COALESCE(project_name, ''))`.
   - **Root Cause**: Missing `isinstance(..., dict)` checks, missing `/index.html` redirect in `serve_static`, missing `project` validation in `proxy_api`, and inconsistent SQL normalization in `delete_project`.

5. **Unknown-project status summary leakage, whitespace project normalization, JS inline-attribute quote escaping, double-click `copySSH` corruption, and unnecessary `settings.html` polling (`internal/ui/app.js`)**:
   - **Input/Context**: Navigating to unowned `/projects/foreign`, VM `project: "   "`, double-clicking `copySSH`, visiting `/settings` while daemon is offline, or inline `onclick` string arguments.
   - **Expected**: Unknown project hides `#project-status-summary`; whitespace VM `project` normalizes to `'default'`; inline handlers escape JS string delimiters before HTML encoding; `/settings` does not poll `/api/vms` every 2s.
   - **Actual**: `#project-status-summary` remained visible with `0 Running`; `"   "` failed to match `'default'`; `escapeHtml` alone left `&#39;` decoded by HTML before JS execution; `copySSH` overwrote `originalHtml` with `"Copied!"`; `/settings` polled `/api/vms` every 2s.
   - **Root Cause**: Missing visibility reset, missing `normalizeProjectName()`, missing `escapeJsString()`, missing `dataset.copying` guard, and unconditional `fetchData()`/`setInterval()`.

## 2. What I changed
- **`frontend/app.py`**: Added `\Z` regex anchor, `isinstance(..., dict)` payload guards across auth & project endpoints, `TRIM(COALESCE(project_name, ''))` parity in `delete_project`, `/index.html` redirect to `/dashboard`/`/login` in `serve_static`, and `is_valid_project_name` validation + trimming for `POST /api/vms` in `proxy_api`.
- **`internal/ui/dashboard.html`**: Added `sm:flex-wrap` and `min-w-[140px]` on `#create-project-form`; removed redundant `block` class on `label[for="rootfs_gb"]`.
- **`internal/ui/project.html`**: Added `min-w-0` to `#ssh-modal-input` and `shrink-0` to `#ssh-modal-copy-btn`; removed redundant `block` class on `label[for="rootfs_gb"]`.
- **`internal/ui/app.js`**: Added `escapeJsString()`, `normalizeProjectName()`, `isValidProjectName()`; fixed daemon-offline (`cachedVMs = null`) project summary counts & `#delete-project-btn` guard; hid `#project-status-summary` on unknown projects; added client-side validation messages for `#create-project-form` and `#spawn-form`; added `whitespace-nowrap` to table toggles; guarded `copySSH`/`copyFromModal` against double-click; gated `fetchData()` polling with `needsLiveData`.
- **`tests/test_frontend_auth.py` & `tests/test_frontend_dom.js`**: Added `test_15_edge_cases_malformed_json_index_redirect_and_spawn_project_validation` and DOM tests 17–19 (offline fallback, unknown project summary hiding, whitespace normalization, `escapeJsString`, `isValidProjectName`).

## 3. Verification Record
- **Deep Verification (ran actual tests):**
  - `./tests/run_tests.sh` -> exited `0` (verified `cryospawn.db` timestamp remained untouched at `Nov 24 23:41`):
    - `[1/3] C++ Core & Ghost Proxy Tests...`: `[✓] ALL DEEP VERIFICATION TESTS PASSED!`
    - `[2/3] Python Flask Frontend & Auth Tests...`: `Ran 15 tests in 1.766s — OK`
    - `[3/3] Frontend UI & DOM Interaction Tests...`: 19/19 assertions passed (`[✓] ALL FRONTEND DOM TESTS PASSED!`)
  - Live HTTP E2E integration script against real Flask (`frontend/app.py`) + isolated temp SQLite DB + mock backend (`http.server`) -> exited `0` (`LIVE_HTTP_E2E_VERIFIED_OK`), verifying `/` -> `/dashboard` -> `/login` redirects, `/index.html` redirect, multi-user registration, spaces in project names (`winter fjord`), cross-tenant `404` isolation, invalid `POST /api/vms` project rejection (`400`), `200` responses & Alpine Daylight markup on all pages, and static asset serving.
  - Node.js `vm.Script` parse check across all 6 HTML templates (`login.html`, `dashboard.html`, `project.html`, `volumes.html`, `networks.html`, `settings.html`) -> exited `0`, zero inline JS syntax errors.
- **Shallow Verification (manual only):**
  - Manual inspection of `internal/ui/style.css` and all 6 HTML templates for Alpine Daylight palette, frosted glass surfaces, wind/snow CSS keyframes, `prefers-reduced-motion`, and responsive grid/flex classes.
- **Unverified aspects:**
  - Live browser visual rendering (Tailwind CDN utility classes, `backdrop-filter` rendering, CSS wind/snow animation smoothness, mobile `390px` & desktop `1440px` computed bounding boxes, live browser console/network logs) — no Chromium, Playwright, Selenium, or installed Firefox binary is available in this environment (`/usr/bin/firefox` is an uninstalled snap stub).
  - Full end-to-end execution against the live root/KVM C++ `cryospawn` daemon + Firecracker microVMs (HTTP E2E used an in-memory HTTP mock for `/api/vms`, `/api/links`, `/api/volumes`).

## 4. Known Issues
- `Shallow Verification` — Real browser CSS/Tailwind rendering, animated cold wind/snow visuals, and in-browser click/polling interactions could not be executed because no headless browser is installed in this environment.
- `Minor Robustness Risk` — All HTML pages load Tailwind CSS from `https://cdn.tailwindcss.com`; air-gapped/offline environments without CDN access will lose Tailwind utility classes beyond `internal/ui/style.css`.
- `Minor Robustness Risk` — Ad-hoc project names created solely via `POST /api/vms` (without `POST /api/projects`) appear dynamically while at least one VM exists in that project, and disappear if that project's last VM is deleted.
- `Minor Robustness Risk` — `internal/ui/index.html` remains on disk with legacy slate styling, though `frontend/app.py` now redirects both `/` and `/index.html` to `/dashboard` (or `/login`).

## 5. Remaining risk & next step
Perform a live visual spot-check in a desktop & mobile browser (e.g. `1440x900` and `390x844`) against the running Flask + C++ `cryospawn` stack to confirm Tailwind CDN layout, Alpine Daylight frosted glass contrast, wind/snow animation smoothness, and end-to-end Firecracker VM lifecycle controls.
