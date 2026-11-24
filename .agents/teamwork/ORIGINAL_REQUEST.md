# Original User Request

## Initial Request — 2026-11-24T18:38:41Z

User requested team preference: "Small, focused team — keep as a cohesive frontend/UI & project-routing upgrade (single implementer + adversarial review; fastest turnaround)".
Redesign the CryoSpawn web dashboard into a Crisp Alpine Daylight winter and cold windy weather aesthetic with a project-first architecture: users land on a dashboard showcasing their owned projects alongside MicroVM deployment, can create new projects, and can tap any project card to open a separate per-project page displaying strictly and exclusively that project's MicroVMs.

Working directory: /home/nirjhar/Python Codes/Einstein/CryoSpawn
Integrity mode: development

## Requirements

### R1. Crisp Alpine Daylight & Cold Wind Weather UI Theme
- Apply a unified winter daylight visual design across the entire web application (login, main dashboard, per-project workspace pages, storage volumes, network routing & mesh, and account settings).
- Visual language must feature bright snow-white and pale-ice translucent/frosted glass card surfaces, crisp frost borders, deep fjord-blue interactive accents, clean high-contrast typography, and subtle animated cold wind streaks / gentle snow drift atmosphere that remain smooth and unobtrusive during normal operation.
- Keep layout spacing, navigation structure, and UI components modular, intuitive, and easy to extend in future releases, with responsive mobile/desktop support.

### R2. Project-First Dashboard & Integrated VM Deployment
- Redesign the primary dashboard landing view so authenticated users first see a clean overview of the projects they own displayed as clickable project cards (showing project name, summary instance counts/status indicators, and quick visual cues), paired seamlessly with the MicroVM deployment section.
- Support both dynamic project discovery (from a user's existing MicroVMs) and explicit empty-project creation (e.g., creating a new project directly from the dashboard so empty project cards persist and remain accessible per user before deploying any MicroVM).
- Maintain strict multi-tenant ownership isolation: a user's project cards, project summaries, and deployment targets must reflect strictly that user's own projects and instances.

### R3. Dedicated Separate Project Pages with Strict Project Scoping
- Tapping or clicking any project card must navigate to a dedicated, bookmarkable project page/route for that project, protected by the same authentication session enforcement as existing protected pages.
- Each dedicated project page must strictly filter and display **only** the MicroVMs belonging to that project (plus a deployment trigger/panel pre-targeted to that project, all existing per-VM controls such as SSH LAN modal copy, HTTP/HTTPS/SSH port toggles, Cross-Project routing opt-in toggle, Start/Wake, Hibernate, Restart, and Delete, and clear navigation back to the main projects overview).
- MicroVMs belonging to any other project (or any other user) must never appear or leak into a project's page.

### R4. Continuity of Existing Platform Workflows
- Preserve full compatibility and clean Alpine Daylight styling for all existing user flows: registration/login/password management, elastic block volume creation and attachment, software-defined inter-VM bridge links and cross-project routing permission controls, LAN-friendly SSH connection command copying, and synchronous VM deletion behavior.

## Verification Resources
- Existing automated regression suite entrypoint: `./tests/run_tests.sh` (runs C++ core/API tests, Python auth & proxy tests, and Node.js frontend DOM unit tests).
- Local test execution must use isolated temporary databases so the workspace's active database is never modified during tests.

## Acceptance Criteria

### Theme & Cross-Page UI Coherence
- [ ] All web pages (login, dashboard, dedicated project view, volumes, networks, settings) share the Crisp Alpine Daylight styling with bright snow/pale-ice frosted surfaces, fjord-blue accents, frost borders, and CSS cold wind / snow atmosphere animations with zero broken assets or console errors.
- [ ] Desktop and mobile viewports render cleanly with consistent navigation links and zero clipping or overlapping interactive toggles/buttons.

### Project Persistence & Multi-Tenant Dashboard Isolation
- [ ] Creating an explicit empty project persists it in storage scoped to the authenticated user and renders a corresponding project card immediately on that user's dashboard even with 0 VMs.
- [ ] Two different authenticated users have completely independent project lists; User A never sees empty projects or VMs belonging to User B.
- [ ] Navigating to a dedicated project route while unauthenticated redirects to login cleanly.

### Dedicated Project Page Strict Scoping
- [ ] Tapping a project card routes to that project's dedicated URL and displays exclusively MicroVMs whose project matches the selected project; VMs in other projects are guaranteed absent from the rendered DOM and table.
- [ ] Deploying a MicroVM from a project's dedicated page pre-targets that project and refreshes the project-scoped view accurately.

### Regression & Automated Test Verification
- [ ] Existing C++ core tests, Python auth/proxy tests (extended to cover project endpoints and route protection), and JavaScript DOM unit tests (extended to cover project cards, empty projects, and strict project-scoped VM filtering) all execute and pass via `./tests/run_tests.sh` with exit code 0.
- [ ] Independent browser/HTTP inspection confirms zero uncaught runtime errors and verifies all visual, navigation, and isolation requirements above.
