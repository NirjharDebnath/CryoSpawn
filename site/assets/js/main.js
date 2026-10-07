/* Page wiring: map toolbar + step log, VM state machine, slot/address calculator. */
(function () {
  gsap.registerPlugin(MotionPathPlugin, ScrollTrigger);
  const esc = s => String(s).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
  const $ = s => document.querySelector(s);

  /* ---------- live map ---------- */
  const Map = window.CSMap;
  Map.render($("#map"));
  Map.apply(CS.world(Object.assign({}, { vm: { 0: "running" }, tap: { 0: true }, sock: { 0: true }, disk: { 0: true } })));

  // toolbar
  const groups = {};
  CS.SCENARIOS.forEach(sc => { (groups[sc.group] = groups[sc.group] || []).push(sc); });
  const bar = $("#scenario-bar");
  Object.keys(groups).forEach(g => {
    const wrap = document.createElement("div");
    wrap.innerHTML = `<div class="group-label">${g}</div><div class="btn-group-wrap"></div>`;
    groups[g].forEach(sc => {
      const b = document.createElement("button");
      b.className = "btn-scn";
      b.dataset.scn = sc.id;
      b.style.setProperty("--c", `var(${sc.c})`);
      b.innerHTML = `<i class="bi ${sc.icon}"></i>${esc(sc.title)}`;
      b.addEventListener("click", () => Map.play(sc.id));
      wrap.lastElementChild.appendChild(b);
    });
    bar.appendChild(wrap);
  });

  const btnPause = $("#btn-pause");
  $("#btn-tour").addEventListener("click", () => Map.startTour());
  btnPause.addEventListener("click", () => {
    const paused = Map.togglePause();
    btnPause.innerHTML = paused ? '<i class="bi bi-play-fill"></i> Resume' : '<i class="bi bi-pause-fill"></i> Pause';
  });
  $("#btn-replay").addEventListener("click", () => Map.current && Map.play(Map.current.id, { tour: Map.tourIdx >= 0 }));
  document.querySelectorAll("[data-speed]").forEach(b => b.addEventListener("click", () => {
    document.querySelectorAll("[data-speed]").forEach(x => x.classList.remove("active"));
    b.classList.add("active");
    Map.setSpeed(parseFloat(b.dataset.speed));
  }));

  // step log
  const logTitle = $("#log-title"), logTrig = $("#log-trig"), logList = $("#log-list");
  Map.onScenarioChange = (sc, i) => {
    document.querySelectorAll(".btn-scn").forEach(b => b.classList.toggle("active", b.dataset.scn === sc.id));
    btnPause.innerHTML = '<i class="bi bi-pause-fill"></i> Pause';
    if (i === -1) {
      logTitle.innerHTML = `<i class="bi ${sc.icon} me-1" style="color:var(${sc.c})"></i>${esc(sc.title)}` +
        (Map.tourIdx >= 0 ? ` <span class="badge rounded-pill text-bg-dark border ms-1" style="font-size:.65rem">tour ${Map.tourIdx + 1}/${CS.TOUR.length}</span>` : "");
      logTrig.textContent = sc.trigger;
      logList.innerHTML = sc.steps.map((st, k) =>
        `<li data-n="${k + 1}">${esc(st.t)}${st.ref ? `<span class="ref">${esc(st.ref)}</span>` : ""}</li>`).join("");
      return;
    }
    [...logList.children].forEach((li, k) => {
      li.classList.toggle("done", k < i);
      li.classList.toggle("now", k === i);
    });
    const now = logList.children[i];
    if (now) logList.scrollTo({ top: now.offsetTop - logList.offsetTop - 40, behavior: "smooth" });
  };

  // start the guided tour once the map is on screen
  ScrollTrigger.create({
    trigger: "#map", start: "top 75%", once: true,
    onEnter: () => { if (!Map.tl) Map.startTour(); }
  });

  /* ---------- flows ---------- */
  CSFlows.render($("#flows"), $("#flow-nav"));

  /* ---------- state machine ---------- */
  const fsm = $("#fsm");
  const NS = "http://www.w3.org/2000/svg";
  const mk = (tag, a, p) => { const n = document.createElementNS(NS, tag); for (const k in a) n.setAttribute(k, a[k]); p.appendChild(n); return n; };
  const STATES = {
    booting:    { x: 170, y: 150, c: "--wake", cap: "Firecracker forked" },
    running:    { x: 400, y: 150, c: "--run",  cap: "guest executing" },
    hibernated: { x: 650, y: 62,  c: "--sleep", cap: "0 CPU · 0 RAM" },
    waking:     { x: 650, y: 238, c: "--wake", cap: "snapshot/load" },
    restarting: { x: 280, y: 262, c: "--wake", cap: "SIGKILL, keep disk" },
    removed:    { x: 400, y: 36,  c: "--deny", cap: "resources freed" }
  };
  const T = [
    ["start", "booting", "M40,150 L105,150", "create_vm()", 70, 140],
    ["booting", "running", "M235,150 L335,150", "InstanceStart", 285, 140],
    ["running", "hibernated", "M465,138 C540,120 560,70 585,66", "hibernate_vm()", 548, 110],
    ["hibernated", "waking", "M650,86 L650,214", "wake_vm()", 690, 154],
    ["waking", "running", "M585,234 C540,230 500,200 450,175", "Resumed", 528, 238],
    ["running", "restarting", "M380,175 L310,238", "reboot_vm()", 380, 222],
    ["restarting", "booting", "M215,262 C170,262 160,220 165,175", "respawn", 158, 240],
    ["running", "removed", "M400,125 L400,60", "terminate / crash", 470, 98],
    ["hibernated", "removed", "M585,56 L465,40", "terminate_vm()", 530, 36]
  ];
  const edgeEls = T.map((t, i) => {
    const p = mk("path", { d: t[2], class: "fsm-edge", id: "fsm-e" + i, "marker-end": "url(#fsm-arrow)" }, fsm);
    mk("text", { x: t[4], y: t[5], class: "fsm-lbl", "text-anchor": "middle" }, fsm).textContent = t[3];
    return p;
  });
  mk("circle", { cx: 32, cy: 150, r: 8, fill: "var(--ice)" }, fsm);
  const stateEls = {};
  for (const k in STATES) {
    const s = STATES[k];
    const g = mk("g", { class: "fsm-state", transform: `translate(${s.x},${s.y})` }, fsm);
    g.style.setProperty("--sc", `var(${s.c})`);
    mk("rect", { x: -65, y: -24, width: 130, height: 48, rx: 12 }, g);
    mk("text", { y: -2, "text-anchor": "middle" }, g).textContent = k;
    mk("text", { y: 14, "text-anchor": "middle", class: "cap" }, g).textContent = s.cap;
    stateEls[k] = g;
  }
  const token = mk("circle", { r: 7, fill: "#fff", style: "filter:drop-shadow(0 0 6px var(--ice))" }, fsm);
  const tour = [
    [0, "<b>create_vm()</b> forks Firecracker; status <b>booting</b> while configure_and_start() runs."],
    [1, "<b>InstanceStart</b> succeeds; status <b>running</b>."],
    [2, "<b>hibernate_vm()</b> pauses, snapshots and kills the process; status <b>hibernated</b>."],
    [3, "Traffic or POST /wake calls <b>wake_vm()</b>; status <b>waking</b> blocks a second wake."],
    [4, "Snapshot loaded and <b>Resumed</b>; back to <b>running</b>."],
    [5, "Volume attach/detach or /restart calls <b>reboot_vm()</b>; status <b>restarting</b> keeps TAP and disk."],
    [6, "A new Firecracker is forked; status <b>booting</b> again."],
    [1, "<b>InstanceStart</b>; <b>running</b>."],
    [7, "DELETE or a crash: the Reaper runs <b>cleanup_vm_resources()</b> and the slot is free again."]
  ];
  const caption = $("#fsm-caption");
  const ftl = gsap.timeline({ repeat: -1, paused: true });
  tour.forEach(([ei, text]) => {
    const [from, to] = T[ei];
    ftl.call(() => {
      Object.values(stateEls).forEach(g => g.classList.remove("lit"));
      edgeEls.forEach(e => e.classList.remove("lit"));
      edgeEls[ei].classList.add("lit");
      caption.innerHTML = text;
    });
    ftl.to(token, { duration: 1, ease: "power1.inOut", motionPath: { path: edgeEls[ei], align: edgeEls[ei], alignOrigin: [0.5, 0.5] } });
    ftl.call(() => { if (stateEls[to]) stateEls[to].classList.add("lit"); });
    ftl.to({}, { duration: 1.6 });
  });
  ScrollTrigger.create({ trigger: fsm, start: "top 85%", onEnter: () => ftl.play(), onLeave: () => ftl.pause(), onEnterBack: () => ftl.play(), onLeaveBack: () => ftl.pause() });

  /* ---------- address calculator ---------- */
  const range = $("#slot-range"), num = $("#slot-num");
  const hex2 = n => n.toString(16).padStart(2, "0").toUpperCase();
  function calc(slot) {
    const base = slot * 4, hi = Math.floor(base / 256), lo = base % 256;
    const ip = d => `172.16.${hi}.${lo + d}`;
    $("#slot-val").textContent = slot;
    $("#c-base").textContent = `${slot} × 4 = ${base}`;
    $("#c-subnet").textContent = `${ip(0)}/30`;
    $("#c-host").textContent = `${ip(1)}  (TAP cryo${slot})`;
    $("#c-guest").textContent = ip(2);
    $("#c-mac").textContent = `AA:FC:AC:10:${hex2(Math.floor(slot / 256))}:${hex2(slot % 256)}`;
    $("#c-api").textContent = `/tmp/cryo_${slot}.socket`;
    $("#c-uds").textContent = `/tmp/cryo_vm_${slot}_22.sock · _80.sock · _443.sock`;
    $("#c-host-name").textContent = `http://vm${slot}.cryo`;
    $("#c-files").textContent = `instances/vm_${slot}_rootfs.ext4 · vm_${slot}_state.snap · vm_${slot}_mem.ram`;
    $("#sb-net").textContent = ip(0);
    $("#sb-gw").textContent = ip(1);
    $("#sb-gst").textContent = ip(2);
    $("#sb-bc").textContent = ip(3);
    gsap.fromTo(".subnet-bar div", { y: 6, opacity: .4 }, { y: 0, opacity: 1, stagger: .05, duration: .3 });
  }
  const sync = v => {
    v = Math.max(0, Math.min(16383, parseInt(v, 10) || 0));
    range.value = Math.min(v, +range.max); num.value = v; calc(v);
  };
  range.addEventListener("input", () => sync(range.value));
  num.addEventListener("input", () => sync(num.value));
  sync(0);

  /* ---------- reveal on scroll ---------- */
  gsap.utils.toArray(".reveal").forEach(n => {
    gsap.from(n, { y: 24, opacity: 0, duration: .7, ease: "power2.out", scrollTrigger: { trigger: n, start: "top 88%", once: true } });
  });
})();
