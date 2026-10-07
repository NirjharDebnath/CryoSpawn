/* Live architecture map: renders nodes/edges, keeps world state, plays scenarios with GSAP. */
(function () {
  const SVGNS = "http://www.w3.org/2000/svg";
  const XHTML = "http://www.w3.org/1999/xhtml";
  const D = 0.85; // seconds per packet hop at 1x

  const el = (tag, attrs, parent) => {
    const n = document.createElementNS(SVGNS, tag);
    for (const k in attrs || {}) n.setAttribute(k, attrs[k]);
    if (parent) parent.appendChild(n);
    return n;
  };
  const cssVar = name => getComputedStyle(document.documentElement).getPropertyValue(name).trim();

  const Map = {
    svg: null, layers: {}, world: null, tl: null, current: null,
    speed: 1, tourIdx: -1, onScenarioChange: null
  };

  /* ---------- rendering ---------- */
  Map.render = function (svg) {
    Map.svg = svg;
    const L = Map.layers;
    L.zones = el("g", {}, svg);
    L.edges = el("g", {}, svg);
    L.labels = el("g", {}, svg);
    L.nodes = el("g", {}, svg);
    L.packets = el("g", {}, svg);

    // zones
    el("rect", { x: 200, y: 16, width: 895, height: 765, rx: 18, class: "zone-rect" }, L.zones);
    el("text", { x: 218, y: 40, class: "zone-label" }, L.zones).textContent = "Linux host · KVM";
    el("text", { x: 18, y: 40, class: "zone-label" }, L.zones).textContent = "Clients";
    el("text", { x: 1115, y: 40, class: "zone-label" }, L.zones).textContent = "Outside";
    el("text", { x: 768, y: 168, class: "zone-label" }, L.zones).textContent = "TAP";
    el("text", { x: 925, y: 168, class: "zone-label" }, L.zones).textContent = "Firecracker";

    // edges
    for (const id in CS.EDGES) {
      const e = CS.EDGES[id];
      el("path", { id, d: e.d, class: "edge" }, L.edges);
      if (e.label) {
        const t = el("text", { x: e.lx, y: e.ly, class: "edge-label", "text-anchor": "middle" }, L.labels);
        t.textContent = e.label;
      }
    }

    // nodes
    for (const id in CS.NODES) {
      const n = CS.NODES[id];
      if (n.vm !== undefined) Map.renderVm(id, n); else Map.renderNode(id, n);
    }

    // link gate
    const g = el("g", { id: "gate", class: "gate blocked", transform: "translate(1000,335)" }, L.nodes);
    el("circle", { r: 13 }, g);
    const fo = el("foreignObject", { x: -13, y: -13, width: 26, height: 26 }, g);
    const d = document.createElementNS(XHTML, "div");
    d.className = "gi";
    d.innerHTML = '<i class="bi bi-x-lg"></i>';
    fo.appendChild(d);
  };

  Map.renderNode = function (id, n) {
    const w = n.w || 150, h = n.h || 60;
    const g = el("g", { id: "n-" + id, class: "node", transform: `translate(${n.x},${n.y})` }, Map.layers.nodes);
    g.style.setProperty("--c", `var(${n.c})`);
    el("rect", { x: -w / 2, y: -h / 2, width: w, height: h, rx: 12, class: "box" }, g);
    const fo = el("foreignObject", { x: -w / 2, y: -h / 2, width: w, height: h }, g);
    const div = document.createElementNS(XHTML, "div");
    div.className = "ndiv";
    div.innerHTML = `<i class="bi ${n.icon}"></i><div><div class="t">${n.label}</div><div class="s">${n.sub}</div></div>`;
    fo.appendChild(div);
    if (n.lamp) el("circle", { cx: w / 2 - 9, cy: -h / 2 + 9, r: 3.5, class: "lamp" }, g);
  };

  Map.renderVm = function (id, n) {
    const w = n.w, h = n.h;
    const g = el("g", { id: "n-" + id, class: "node vm st-absent", transform: `translate(${n.x},${n.y})` }, Map.layers.nodes);
    el("rect", { x: -w / 2, y: -h / 2, width: w, height: h, rx: 14, class: "box" }, g);
    // frost overlay for hibernation
    const frost = el("g", { class: "frost" }, g);
    for (let i = 0; i < 6; i++) {
      el("text", { x: -w / 2 + 14 + i * 24, y: h / 2 - 8 - (i % 2) * 6, fill: "var(--sleep)", "font-size": 10, opacity: .55 }, frost).textContent = "❄";
    }
    const fo = el("foreignObject", { x: -w / 2, y: -h / 2, width: w, height: h }, g);
    const div = document.createElementNS(XHTML, "div");
    div.className = "vdiv";
    const slot = n.vm, base = slot * 4;
    div.innerHTML = `
      <div class="vhead"><span><i class="bi ${n.icon}"></i>${n.label}</span><span class="badge-st">free</span></div>
      <div class="vrow"><span>guest</span><b>172.16.0.${base + 2}</b></div>
      <div class="vrow"><span>process</span><b class="proc">—</b></div>
      <div class="vrow"><span>drives</span><b class="drv">—</b></div>
      <div class="vrow"><span>host RAM</span><b class="ram">0</b></div>
      <div class="meter"><span></span></div>`;
    fo.appendChild(div);
  };

  /* ---------- world state ---------- */
  const PROC = { running: "firecracker", booting: "firecracker", waking: "firecracker", restarting: "SIGKILL", paused: "paused", hibernated: "none", dead: "exited", absent: "—" };
  const RAM = { running: 100, booting: 45, waking: 70, restarting: 20, paused: 100, hibernated: 0, dead: 0, absent: 0 };
  const LABEL = { absent: "free", running: "running", booting: "booting", waking: "waking", restarting: "restarting", paused: "paused", hibernated: "hibernated", dead: "dead" };

  Map.apply = function (w) {
    Map.world = w;
    const node = id => document.getElementById("n-" + id);
    const toggle = (id, cls, on) => node(id).classList.toggle(cls, !!on);

    [0, 1].forEach(i => {
      const g = node("vm" + i), st = w.vm[i];
      g.setAttribute("class", "node vm st-" + st);
      g.querySelector(".badge-st").textContent = LABEL[st];
      g.querySelector(".proc").textContent = PROC[st];
      const drives = [];
      if (w.disk[i]) drives.push("rootfs");
      if (w.vol === i) drives.push("vol_1");
      g.querySelector(".drv").textContent = drives.length ? drives.join(" + ") : "—";
      g.querySelector(".ram").textContent = RAM[st] ? "in use" : "0";
      g.querySelector(".meter span").style.width = RAM[st] + "%";
      toggle("tap" + i, "gone", !w.tap[i]);
      toggle("tap" + i, "on", w.tap[i]);
      document.getElementById("e-tap" + i + "-vm" + i).classList.toggle("off", !w.tap[i]);
      document.getElementById("e-nf-tap" + i).classList.toggle("off", !w.tap[i]);
      document.getElementById("e-gh-tap" + i).classList.toggle("off", !w.sock[i]);
      document.getElementById("e-mgr-vm" + i).classList.toggle("off", st === "absent");
    });

    const anySock = w.sock[0] || w.sock[1];
    toggle("uds", "on", anySock);
    toggle("ghost", "on", anySock);
    toggle("nginx", "on", true);
    toggle("api", "on", w.ready.api);
    toggle("nf", "on", w.ready.nf);
    toggle("reaper", "on", w.ready.reaper);
    toggle("reaper", "pulse", w.ready.reaper);
    toggle("api", "ghosted", !w.ready.api);
    toggle("reaper", "ghosted", !w.ready.reaper);
    toggle("vols", "on", w.volExists);
    document.getElementById("e-vols-vm1").classList.toggle("off", w.vol !== 1);

    const gate = document.getElementById("gate");
    gate.setAttribute("class", "gate " + (w.link ? "open" : "blocked"));
    gate.querySelector(".gi").innerHTML = w.link ? '<i class="bi bi-check-lg"></i>' : '<i class="bi bi-x-lg"></i>';
    document.getElementById("e-link").classList.toggle("flow", w.link);
    gate.style.opacity = (w.vm[0] !== "absent" && w.vm[1] !== "absent") ? 1 : 0.25;

    Map.updateCounters(w);
  };

  Map.updateCounters = function (w) {
    const live = s => ["running", "booting", "waking", "paused"].includes(s);
    const vals = {
      fc: w.vm.filter(live).length,
      hib: w.vm.filter(s => s === "hibernated").length,
      tap: w.tap.filter(Boolean).length,
      sock: w.sock.filter(Boolean).length,
      files: w.disk.filter(Boolean).length + w.snap.filter(Boolean).length * 2 + (w.volExists ? 1 : 0),
      rules: w.link ? 4 : 0
    };
    for (const k in vals) {
      const box = document.querySelector(`[data-counter="${k}"]`);
      if (!box) continue;
      const v = box.querySelector(".num");
      if (v.textContent !== String(vals[k])) {
        v.textContent = vals[k];
        box.classList.remove("bump"); void box.offsetWidth; box.classList.add("bump");
      }
    }
  };

  /* ---------- scenario playback ---------- */
  function hop(item) {
    if (typeof item === "string") {
      const rev = item[0] === "-";
      return { e: rev ? item.slice(1) : item, rev, stop: 1, deny: false };
    }
    return Object.assign({ rev: false, stop: 1, deny: false }, item);
  }

  Map.stop = function () {
    if (Map.tl) { Map.tl.kill(); Map.tl = null; }
    Map.layers.packets.innerHTML = "";
    document.querySelectorAll(".edge.hot").forEach(e => e.classList.remove("hot"));
    document.querySelectorAll(".node.flash").forEach(e => e.classList.remove("flash"));
  };

  Map.play = function (id, opts) {
    opts = opts || {};
    const sc = CS.byId(id);
    if (!sc) return;
    Map.stop();
    Map.current = sc;
    if (!opts.tour) Map.tourIdx = -1;
    const world = CS.world(sc.setup);
    Map.apply(world);
    if (Map.onScenarioChange) Map.onScenarioChange(sc, -1);

    const reduce = window.matchMedia("(prefers-reduced-motion: reduce)").matches;
    const tl = gsap.timeline({
      onComplete: () => {
        if (Map.onScenarioChange) Map.onScenarioChange(sc, sc.steps.length);
        if (Map.tourIdx >= 0) {
          Map.tourIdx = (Map.tourIdx + 1) % CS.TOUR.length;
          gsap.delayedCall(1.4, () => { if (Map.tourIdx >= 0) Map.play(CS.TOUR[Map.tourIdx], { tour: true }); });
        }
      }
    });
    tl.timeScale(Map.speed);
    let cursor = 0.4;

    sc.steps.forEach((step, i) => {
      const flashes = step.f || [];
      tl.call(() => {
        if (Map.onScenarioChange) Map.onScenarioChange(sc, i);
        flashes.forEach(n => document.getElementById("n-" + n).classList.add("flash"));
      }, null, cursor);

      let longest = 0;
      (step.p || []).forEach(group => {
        const chain = Array.isArray(group) ? group : [group];
        let t = cursor;
        chain.forEach(raw => {
          const h = hop(raw);
          const path = document.getElementById(h.e);
          const color = cssVar(h.deny ? "--ice" : (step.c || CS.edgeColor(h.e)));
          const dot = el("circle", { r: 6, fill: color, class: "packet", opacity: 0 }, Map.layers.packets);
          dot.style.color = color;
          const from = h.rev ? 1 : 0, to = h.rev ? 1 - h.stop : h.stop;
          const dur = D * Math.max(h.stop, 0.5) * (reduce ? 0.6 : 1);
          tl.call(() => { path.classList.add("hot"); path.style.setProperty("--c", color); }, null, t);
          tl.set(dot, { opacity: 1 }, t);
          tl.to(dot, {
            duration: dur, ease: "power1.inOut",
            motionPath: { path, align: path, alignOrigin: [0.5, 0.5], start: from, end: to }
          }, t);
          if (h.deny) {
            const red = cssVar("--deny");
            tl.to(dot, { attr: { r: 14 }, fill: red, opacity: 0, duration: 0.5 }, t + dur);
            tl.call(() => {
              const g = document.getElementById("gate");
              gsap.fromTo(g, { scale: 1.4, transformOrigin: "50% 50%" }, { scale: 1, duration: 0.5 });
            }, null, t + dur);
            t += dur + 0.5;
          } else {
            tl.set(dot, { opacity: 0 }, t + dur);
            t += dur;
          }
          tl.call(() => path.classList.remove("hot"), null, t + 0.05);
        });
        longest = Math.max(longest, t - cursor);
      });

      const hold = Math.max(longest, 0.9) + 0.35;
      tl.call(() => {
        flashes.forEach(n => document.getElementById("n-" + n).classList.remove("flash"));
        if (step.s) Map.apply(CS.patchWorld(Map.world, step.s));
      }, null, cursor + hold);
      cursor += hold + 0.75;
    });
    tl.to({}, { duration: 0.2 }, cursor);
    Map.tl = tl;
  };

  Map.startTour = function () {
    Map.tourIdx = 0;
    Map.play(CS.TOUR[0], { tour: true });
  };

  Map.setSpeed = function (s) {
    Map.speed = s;
    if (Map.tl) Map.tl.timeScale(s);
  };

  Map.togglePause = function () {
    if (!Map.tl) return false;
    Map.tl.paused(!Map.tl.paused());
    return Map.tl.paused();
  };

  window.CSMap = Map;
})();
