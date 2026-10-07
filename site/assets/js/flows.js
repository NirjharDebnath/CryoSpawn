/* Builds an animated sequence diagram for every scenario from the same data the live map uses. */
(function () {
  const esc = s => String(s).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
  const actorsOf = sc => {
    const used = new Set();
    sc.steps.forEach(st => {
      (st.p || []).forEach(g => (Array.isArray(g) ? g : [g]).forEach(raw => {
        const id = typeof raw === "string" ? raw.replace(/^-/, "") : raw.e;
        used.add(CS.EDGES[id].from); used.add(CS.EDGES[id].to);
      }));
      (st.f || []).forEach(n => used.add(n));
    });
    return CS.ACTOR_ORDER.filter(a => used.has(a));
  };

  const arrowsOf = st => {
    const out = [];
    (st.p || []).forEach(g => (Array.isArray(g) ? g : [g]).forEach(raw => {
      const h = typeof raw === "string" ? { e: raw.replace(/^-/, ""), rev: raw[0] === "-" } : raw;
      const e = CS.EDGES[h.e];
      out.push({ from: h.rev ? e.to : e.from, to: h.rev ? e.from : e.to, deny: !!h.deny, c: CS.edgeColor(h.e) });
    }));
    return out;
  };

  const actorName = id => {
    const n = CS.NODES[id];
    return n.label;
  };

  function build(sc, idx) {
    const actors = actorsOf(sc);
    const pos = {};
    actors.forEach((a, i) => { pos[a] = ((i + 0.5) / actors.length) * 100; });

    const wrap = document.createElement("article");
    wrap.className = "panel flow";
    wrap.id = "flow-" + sc.id;
    wrap.style.setProperty("--c", `var(${sc.c})`);

    const lifelines = actors.map(a => `<div class="seq-life" style="left:${pos[a]}%"></div>`).join("");
    const head = actors.map((a, i) => {
      const n = CS.NODES[a];
      return `<div class="seq-actor${i % 2 ? " low" : ""}" style="left:${pos[a]}%;--c:var(${n.c})" title="${n.label} · ${n.sub || ""}">
        <div class="a-ico"><i class="bi ${n.icon}"></i></div><div class="a-lbl">${actorName(a)}</div></div>`;
    }).join("");

    const rows = sc.steps.map((st, i) => {
      const arrows = arrowsOf(st);
      const h = Math.max(46, 22 + arrows.length * 12);
      let marks = "";
      arrows.forEach((a, k) => {
        const top = arrows.length === 1 ? 50 : ((k + 1) / (arrows.length + 1)) * 100;
        const x1 = pos[a.from], x2 = pos[a.to];
        const left = Math.min(x1, x2), width = Math.abs(x2 - x1);
        const dir = x2 >= x1 ? "r" : "l";
        if (a.deny) {
          const half = width / 2;
          marks += `<div class="seq-arrow ${dir}" data-dir="${dir}" style="top:${top}%;left:${dir === "r" ? left : left + half}%;width:${half}%;--c:var(--deny)"></div>
                    <div class="seq-self" style="top:${top}%;left:${left + half}%;--c:var(--deny)"></div>`;
        } else {
          marks += `<div class="seq-arrow ${dir}" data-dir="${dir}" style="top:${top}%;left:${left}%;width:${width}%;--c:var(${a.c})"></div>
                    <div class="seq-dot" data-x1="${x1}" data-x2="${x2}" style="top:${top}%;left:${x1}%;--c:var(${a.c})"></div>`;
        }
      });
      (st.f || []).forEach(n => {
        if (!arrows.length) marks += `<div class="seq-self" style="left:${pos[n]}%;--c:var(${CS.NODES[n].c})"></div>`;
      });
      const ref = st.ref ? `<span class="ref">${esc(st.ref)}</span>` : "";
      return `<div class="seq-row" data-i="${i}">
        <div class="seq-lanes" style="min-height:${h}px">${lifelines}${marks}</div>
        <div class="seq-text"><span class="n">${String(i + 1).padStart(2, "0")}</span><span>${esc(st.t)}${ref}</span></div>
      </div>`;
    }).join("");

    wrap.innerHTML = `
      <div class="flow-title">
        <div class="ico"><i class="bi ${sc.icon}"></i></div>
        <div><h3>${String(idx + 1).padStart(2, "0")} · ${esc(sc.title)}</h3><div class="trig">${esc(sc.trigger)}</div></div>
        <div class="flow-actions">
          <button class="btn-ctl" data-replay title="Replay this diagram"><i class="bi bi-arrow-counterclockwise"></i> Replay</button>
          <button class="btn-ctl" data-onmap="${sc.id}" title="Play on the live map"><i class="bi bi-diagram-3"></i> Play on map</button>
        </div>
      </div>
      <p class="text-muted-2 mt-3 mb-0">${esc(sc.summary)}</p>
      <div class="seq"><div class="seq-inner" style="--lanes-min:${Math.max(300, actors.length * 64)}px">
        <div class="seq-head"><div class="seq-row"><div class="seq-lanes">${head}</div><div></div></div></div>
        <div class="seq-body">${rows}</div>
      </div></div>
      ${sc.note ? `<div class="flow-note"><i class="bi bi-info-circle me-1"></i>${esc(sc.note)}</div>` : ""}`;
    return wrap;
  }

  function animateRow(row) {
    row.classList.add("in");
    const arrows = row.querySelectorAll(".seq-arrow");
    const dots = row.querySelectorAll(".seq-dot");
    arrows.forEach(a => {
      gsap.fromTo(a, { scaleX: 0, transformOrigin: a.dataset.dir === "r" ? "left center" : "right center" },
        { scaleX: 1, duration: 0.6, ease: "power2.out" });
    });
    dots.forEach((d, k) => {
      gsap.fromTo(d, { left: d.dataset.x1 + "%", opacity: 1 },
        { left: d.dataset.x2 + "%", duration: 0.9, delay: 0.15 + k * 0.12, ease: "power1.inOut",
          onComplete: () => gsap.to(d, { opacity: 0, duration: 0.3 }) });
    });
  }

  function replay(flow) {
    const rows = [...flow.querySelectorAll(".seq-body .seq-row")];
    rows.forEach(r => r.classList.remove("in"));
    rows.forEach((r, i) => gsap.delayedCall(0.25 + i * 0.45, () => animateRow(r)));
  }

  window.CSFlows = {
    render(container, nav) {
      CS.SCENARIOS.forEach((sc, i) => {
        container.appendChild(build(sc, i));
        const a = document.createElement("a");
        a.href = "#flow-" + sc.id;
        a.innerHTML = `<i class="bi ${sc.icon} me-1"></i>${sc.title}`;
        nav.appendChild(a);
      });

      container.querySelectorAll(".seq-body .seq-row").forEach(row => {
        ScrollTrigger.create({ trigger: row, start: "top 88%", once: true, onEnter: () => animateRow(row) });
      });
      container.addEventListener("click", ev => {
        const r = ev.target.closest("[data-replay]");
        if (r) replay(r.closest(".flow"));
        const m = ev.target.closest("[data-onmap]");
        if (m) {
          document.getElementById("map-section").scrollIntoView({ behavior: "smooth" });
          window.CSMap.play(m.dataset.onmap);
        }
      });
    }
  };
})();
