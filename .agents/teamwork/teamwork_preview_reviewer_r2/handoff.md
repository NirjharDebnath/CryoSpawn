# CryoSpawn Adversarial Review Round 2 Handoff

> [!WARNING] **Skepticism Disclaimer**
> C++ core tests, 16 Python Flask auth/project tests, 23 Node.js DOM tests, inline HTML script checks, HTML structural validation, and a live compiled C++ daemon + Flask HTTP E2E integration run all passed against isolated temporary databases; however, real Chromium/Firefox browser layout/CSS rendering (Tailwind CDN, `backdrop-filter`, CSS wind/snow animations, mobile `390px` vs desktop `1440px` computed geometry) and live Firecracker KVM boot remain unverified because no browser binary is installed in this environment.

## 1. What the prior attempt got wrong
1. **`POST /api/vms` with JSON `project: null` crashed C++ JSON parsing (`frontend/app.py`)**:
   - **Input**: Authenticated `POST /api/vms` with `{"name": "vm-null", "project": null}` (or trailing slash `/api/vms/` with invalid `project`).
   - **Expected**: `project: null` normalizes to `'default'` (matching omitted or empty `project`), and `/api/vms/` validates `project` just like `/api/vms`.
   - **Actual**: `if raw_proj is not None:` skipped `None` and forwarded `"project": null` to `internal/server.hpp:200`, where `std::string project = body["project"];` threw `nlohmann::json::type_error` -> HTTP `400 {"error": "Invalid JSON"}`; trailing-slash `/api/vms/` bypassed `path == 'vms'`.
   - **Root Cause**: Unhandled `raw_proj is None` branch and exact `path == 'vms'` string check in `proxy_api()`.

2. **SSH modal `Copy` button emerald feedback invisible over `.alpine-btn-primary` gradient (`internal/ui/app.js`, `internal/ui/style.css`)**:
   - **Input**: Clicking `#ssh-modal-copy-btn` (`Copy`) inside `#ssh-modal` in `internal/ui/project.html`.
   - **Expected**: Button text switches to `"Copied!"` and background visually turns emerald green.
   - **Actual**: `copyFromModal()` added `bg-emerald-600` (`background-color`), which paints underneath `.alpine-btn-primary`'s `background: linear-gradient(...)` (`background-image`), leaving the button blue.
   - **Root Cause**: CSS `background-image` from `.alpine-btn-primary` overrides Tailwind's `bg-emerald-600` `background-color`. Fixed by adding `.alpine-btn-success` in `style.css` and toggling it in `copyFromModal()`.

3. **Out-of-order `fetchData()` polling race condition (`internal/ui/app.js`)**:
   - **Input**: Triggering a post-mutation refresh (`spawnForm`, `createProjectForm`, `deleteVM`, etc.) while an earlier 2-second `fetchData()` poll is still in flight and resolves later.
   - **Expected**: Stale poll responses must never overwrite newer post-mutation UI state.
   - **Actual**: `fetchData()` had no monotonic sequence check; a slower earlier poll resolving after a faster post-mutation `fetchData()` overwrote `cachedVMs`, `lastRenderState`, and the rendered DOM with stale data.
   - **Root Cause**: Missing `fetchSeq` / `latestAppliedSeq` guard in `fetchData()`.

4. **Inconsistent whitespace `project` normalization across renderers and network selectors (`internal/ui/app.js`)**:
   - **Input**: A VM with `project: "   "` alongside a VM with `project: "default"`.
   - **Expected**: Both display `Project: default` and are grouped as `Same Project (default)` in `updateDestinationSelector()`.
   - **Actual**: `renderTable()`, `updateLinkSelectors()`, `updateDestinationSelector()`, `renderVolumes()`, and `renderRoutingPermissions()` used `vm.project || 'default'` instead of `normalizeProjectName(vm.project)`, treating `"   "` as truthy and separating `"   "` from `"default"`.
   - **Root Cause**: Partial rollout of `normalizeProjectName()` in Round 1.

5. **Missing offline daemon banners on `/volumes` & `/networks`, mobile `<select>` overflow risk, initial `#project-workspace` flash, invalid `#create-missing-project-btn`, unhandled fetch rejections, and non-JSON error crashes (`volumes.html`, `networks.html`, `project.html`, `login.html`, `settings.html`, `app.js`)**:
   - **Input**: Visiting `/volumes` or `/networks` with C++ daemon offline; long VM/project names inside `renderVolumes()` `<select>` at `390px` width; visiting `/projects/bad!name`; non-JSON 502 responses on `login.html`, `settings.html`, or `volumeForm`; network errors on VM/volume actions.
   - **Expected**: Offline banner visible on `/volumes` & `/networks`; `<select>` bounded with `min-w-0 max-w-full`; `#project-workspace` starts `hidden` until verified and `#create-missing-project-btn` hides for invalid project names; action `fetch()` calls wrapped in `try/catch` with `encodeURIComponent` and safe `.json().catch(() => ({}))`.
   - **Actual**: `/volumes` & `/networks` silently showed empty states without `#daemon-status-banner`; `<select>` lacked `min-w-0 max-w-full`; `#project-workspace` flashed visible before `#project-not-found`; invalid route `/projects/bad!name` offered `Create This Project`; `deleteVM`/`restartVM`/`hibernateVM`/`wakeVM`/`deleteVolume`/`attachVolume`/`detachVolume` lacked `try/catch` and URL encoding; `login.html`, `settings.html`, and `volumeForm` used bare `await res.json()`.
   - **Root Cause**: Omitted banners, flex constraints, initial `hidden` state, validation gating, and error-handling wrappers.

*(Note: Live C++ + Flask testing also surfaced a pre-existing bug in `internal/server.hpp:750`, where `POST /api/volumes` returns `json{{"status", "created", "id", id}}` — a JSON array `["status","created","id",id]` instead of a JSON object — though `app.js` ignores the body fields on `res.ok` and refreshes via `GET /api/volumes`.)*

## 2. What I changed
- **`frontend/app.py`**: Normalized `project: None` to `'default'` and handled `path.rstrip('/') == 'vms'` in `proxy_api()`.
- **`internal/ui/style.css`**: Added `.alpine-btn-success` (`!important` emerald gradient) to override `.alpine-btn-primary` during modal copy feedback.
- **`internal/ui/project.html`**: Added `hidden` to `#project-workspace` initially to avoid flashing an unowned workspace prior to `fetchData()`.
- **`internal/ui/volumes.html` & `internal/ui/networks.html`**: Added `#daemon-status-banner`; added `max-w-full` constraints on network `<select>` elements.
- **`internal/ui/login.html` & `internal/ui/settings.html`**: Hardened `await res.json().catch(() => ({}))` for non-JSON error responses.
- **`internal/ui/app.js`**: Added `fetchSeq`/`latestAppliedSeq` out-of-order guard in `fetchData()`, gated `#create-missing-project-btn` with `isValidProjectName()`, guarded `vm.uuid` in `renderTable()`, applied `normalizeProjectName()` consistently in `renderTable()`, `updateLinkSelectors()`, `updateDestinationSelector()`, `renderVolumes()`, and `renderRoutingPermissions()`, added `min-w-0 max-w-full` to volume attach `<select>`, added `try/catch` + `encodeURIComponent` to VM/volume action handlers, hardened `volumeForm` `.json().catch()`, and updated `copyFromModal()` to toggle `alpine-btn-success`.
- **`tests/test_frontend_auth.py` & `tests/test_frontend_dom.js`**: Added `test_16_null_project_normalization_and_template_banners` and DOM tests 20–23.

## 3. Verification Record
- **Deep Verification (ran actual tests):**
  - `./tests/run_tests.sh` -> exited `0` (verified `cryospawn.db` untouched at `Nov 24 23:41`):
    - `[1/3] C++ Core & Ghost Proxy Tests...`: `[✓] ALL DEEP VERIFICATION TESTS PASSED!`
    - `[2/3] Python Flask Frontend & Auth Tests...`: `Ran 16 tests in 1.880s — OK` (zero warnings)
    - `[3/3] Frontend UI & DOM Interaction Tests...`: 23/23 assertions passed (`[✓] ALL FRONTEND DOM TESTS PASSED!`)
  - Live compiled C++ daemon (`internal/main.cpp`) + real Flask (`frontend/app.py`) HTTP E2E test using isolated temp `CRYOSPAWN_BASE_PATH` and temp SQLite DB -> exited `0` (`LIVE_CPP_AND_FLASK_E2E_VERIFIED_OK`), verifying unauthenticated `/projects/winter%20fjord` redirect (`302`), multi-user isolation, real C++ `/api/vms`, `/api/links`, `/api/volumes` (`POST`/`GET`/cross-tenant `403`/`DELETE`), invalid VM project rejection (`400`), and `200` Alpine Daylight markup on all 6 routes.
  - Node.js `vm.Script` inline script syntax check across all 6 HTML templates -> exited `0`.
  - Python `HTMLParser` structural/tag/handler/dark-remnant audit across all 6 HTML templates -> exited `0` with zero errors or dark-theme remnants.
- **Shallow Verification (manual only):**
  - Manual CSS/Tailwind class inspection of `style.css`, `app.js`, and all 6 HTML templates.
- **Unverified aspects:**
  - Real Chromium/Firefox visual rendering (Tailwind CDN utility evaluation, `backdrop-filter`, CSS wind/snow animation smoothness, `390px` mobile & `1440px` desktop bounding boxes, live browser console/network logs) — no browser engine is installed in this environment (`/usr/bin/firefox` is an uninstalled snap stub).
  - Live Firecracker KVM microVM boot/hibernation/SSH-proxy execution inside a root/KVM host.

## 4. Known Issues
- `Shallow Verification` — Real browser CSS/Tailwind rendering, animated cold wind/snow visuals, and interactive DOM layout at desktop (`1440px`) and mobile (`390px`) could not be executed due to the absence of an installed browser binary.
- `Minor Robustness Risk` — All HTML pages load Tailwind CSS from `https://cdn.tailwindcss.com`; air-gapped/offline environments without CDN access lose Tailwind utility classes beyond `internal/ui/style.css`.
- `Minor Robustness Risk` — Pre-existing `internal/server.hpp:750` `POST /api/volumes` returns a JSON array (`json{{"status", "created", "id", id}}`) instead of a JSON object; the UI is unaffected because it refreshes via `GET /api/volumes`.
- `Minor Robustness Risk` — Unused `internal/ui/index.html` remains on disk with legacy slate styling, though `/` and `/index.html` both redirect away from it.
- `Minor Robustness Risk` — Ad-hoc projects created solely via `POST /api/vms` (without `POST /api/projects`) appear dynamically while VMs exist and disappear if their last VM is deleted.

## 5. Remaining risk & next step
Spot-check the running Flask + C++ `cryospawn` stack in a real desktop (`1440x900`) and mobile (`390x844`) browser to visually verify Tailwind CDN layout, Alpine Daylight frosted glass contrast, cold wind/snow animations, and live KVM Firecracker VM lifecycle controls.
