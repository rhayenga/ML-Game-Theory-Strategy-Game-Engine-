// Catan Coach front-end: board render, advice UI, autoplay, game recap.

const COLORS = {
  hills: "#c47a4a",
  forest: "#2f7a4b",
  mountains: "#7a8794",
  fields: "#d4b84a",
  pasture: "#7bbf5a",
  desert: "#c9b896",
};
const PLAYER = [
  { fill: "#ff2d2d", stroke: "#4a0000" },
  { fill: "#ffffff", stroke: "#1a1a1a" },
  { fill: "#ff9500", stroke: "#5a2a00" },
  { fill: "#2f6bff", stroke: "#0a1f66" },
];
const RES = ["brick", "lumber", "ore", "grain", "wool"];

let state = null;
let lastAdvice = null;
let topMoves = [];
let selectedIdx = 0;
let autoRunning = false;
let autoAbort = false;
let lastRecapKey = null;

const $ = (id) => document.getElementById(id);

async function api(path, body = {}) {
  const res = await fetch(path, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body),
  });
  const data = await res.json();
  if (!data.ok) throw new Error(data.error || "request failed");
  return data;
}

function toast(msg) {
  const el = $("toast");
  el.textContent = msg;
  el.classList.remove("hidden");
  clearTimeout(toast._t);
  toast._t = setTimeout(() => el.classList.add("hidden"), 2200);
}

function hexPolygon(cx, cy, size) {
  const pts = [];
  for (let i = 0; i < 6; i++) {
    const a = (Math.PI / 180) * (60 * i);
    pts.push(`${cx + size * Math.cos(a)},${cy + size * Math.sin(a)}`);
  }
  return pts.join(" ");
}

function selectedAction() {
  if (!topMoves.length) return lastAdvice;
  return topMoves[selectedIdx]?.action || lastAdvice;
}

function renderBoard(s) {
  const svg = $("board");
  const ns = "http://www.w3.org/2000/svg";
  while (svg.firstChild) svg.removeChild(svg.firstChild);

  const size = 42;

  const bg = document.createElementNS(ns, "circle");
  bg.setAttribute("cx", "0");
  bg.setAttribute("cy", "0");
  bg.setAttribute("r", "250");
  bg.setAttribute("fill", "rgba(45, 95, 88, 0.45)");
  svg.appendChild(bg);

  for (const h of s.hexes) {
    const g = document.createElementNS(ns, "g");
    const poly = document.createElementNS(ns, "polygon");
    poly.setAttribute("points", hexPolygon(h.x, h.y, size - 1.5));
    poly.setAttribute("fill", COLORS[h.terrain] || "#888");
    poly.setAttribute("stroke", "rgba(40, 28, 18, 0.75)");
    poly.setAttribute("stroke-width", "2.25");
    if (h.id === s.robber) poly.setAttribute("filter", "brightness(0.55)");
    g.appendChild(poly);

    if (h.number) {
      const circ = document.createElementNS(ns, "circle");
      circ.setAttribute("cx", h.x);
      circ.setAttribute("cy", h.y);
      circ.setAttribute("r", "12");
      circ.setAttribute("fill", "#f3ead2");
      g.appendChild(circ);
      const t = document.createElementNS(ns, "text");
      t.setAttribute("x", h.x);
      t.setAttribute("y", h.y + 4);
      t.setAttribute("text-anchor", "middle");
      t.setAttribute("class", `hex-number${h.number === 6 || h.number === 8 ? " hot" : ""}`);
      t.textContent = String(h.number);
      g.appendChild(t);
    }

    if (h.id === s.robber) {
      const r = document.createElementNS(ns, "circle");
      r.setAttribute("cx", h.x);
      r.setAttribute("cy", h.y + (h.number ? 20 : 0));
      r.setAttribute("r", "7");
      r.setAttribute("fill", "#1a1a1a");
      g.appendChild(r);
    }
    svg.appendChild(g);
  }

  for (const e of s.edges) {
    if (e.owner < 0) continue;
    const col = PLAYER[e.owner];
    const outline = document.createElementNS(ns, "line");
    outline.setAttribute("x1", e.x0);
    outline.setAttribute("y1", e.y0);
    outline.setAttribute("x2", e.x1);
    outline.setAttribute("y2", e.y1);
    outline.setAttribute("stroke", col.stroke);
    outline.setAttribute("stroke-width", "8");
    outline.setAttribute("stroke-linecap", "round");
    svg.appendChild(outline);
    const line = document.createElementNS(ns, "line");
    line.setAttribute("x1", e.x0);
    line.setAttribute("y1", e.y0);
    line.setAttribute("x2", e.x1);
    line.setAttribute("y2", e.y1);
    line.setAttribute("stroke", col.fill);
    line.setAttribute("stroke-width", "5");
    line.setAttribute("stroke-linecap", "round");
    svg.appendChild(line);
  }

  for (const v of s.vertices) {
    if (v.owner >= 0) continue;
    const c = document.createElementNS(ns, "circle");
    c.setAttribute("cx", v.x);
    c.setAttribute("cy", v.y);
    c.setAttribute("r", "3.2");
    c.setAttribute("fill", "rgba(60,45,30,0.18)");
    c.setAttribute("stroke", "rgba(40,28,18,0.4)");
    c.setAttribute("stroke-width", "1");
    svg.appendChild(c);
  }

  for (const v of s.vertices) {
    if (v.owner < 0) continue;
    const col = PLAYER[v.owner];
    if (v.city) {
      const rect = document.createElementNS(ns, "rect");
      rect.setAttribute("x", v.x - 9);
      rect.setAttribute("y", v.y - 9);
      rect.setAttribute("width", "18");
      rect.setAttribute("height", "18");
      rect.setAttribute("rx", "2");
      rect.setAttribute("fill", col.fill);
      rect.setAttribute("stroke", col.stroke);
      rect.setAttribute("stroke-width", "2.25");
      svg.appendChild(rect);
    } else {
      const poly = document.createElementNS(ns, "polygon");
      const x = v.x, y = v.y;
      poly.setAttribute("points", `${x},${y - 10} ${x + 9},${y + 6} ${x - 9},${y + 6}`);
      poly.setAttribute("fill", col.fill);
      poly.setAttribute("stroke", col.stroke);
      poly.setAttribute("stroke-width", "2.25");
      svg.appendChild(poly);
    }
  }

  const a = selectedAction();
  if (a) {
    if (a.type === "BuildSettlement" || a.type === "BuildCity") {
      const v = s.vertices[a.a];
      if (v) {
        const ring = document.createElementNS(ns, "circle");
        ring.setAttribute("cx", v.x);
        ring.setAttribute("cy", v.y);
        ring.setAttribute("r", "14");
        ring.setAttribute("fill", "none");
        ring.setAttribute("stroke", "#f0b429");
        ring.setAttribute("stroke-width", "3");
        ring.setAttribute("stroke-dasharray", "4 3");
        svg.appendChild(ring);
      }
    }
    if (a.type === "BuildRoad" || a.type === "PlayRoadBuilding") {
      const e = s.edges[a.a];
      if (e) {
        const line = document.createElementNS(ns, "line");
        line.setAttribute("x1", e.x0);
        line.setAttribute("y1", e.y0);
        line.setAttribute("x2", e.x1);
        line.setAttribute("y2", e.y1);
        line.setAttribute("stroke", "#f0b429");
        line.setAttribute("stroke-width", "8");
        line.setAttribute("stroke-linecap", "round");
        line.setAttribute("opacity", "0.55");
        svg.appendChild(line);
      }
    }
    if (a.type === "PlaceRobber" || a.type === "PlayKnight") {
      const h = s.hexes[a.a];
      if (h) {
        const ring = document.createElementNS(ns, "circle");
        ring.setAttribute("cx", h.x);
        ring.setAttribute("cy", h.y);
        ring.setAttribute("r", size - 4);
        ring.setAttribute("fill", "none");
        ring.setAttribute("stroke", "#f0b429");
        ring.setAttribute("stroke-width", "3");
        svg.appendChild(ring);
      }
    }
  }
}

function renderTopList(data) {
  const list = $("topList");
  list.innerHTML = "";
  topMoves = Array.isArray(data?.top) ? data.top.slice(0, 3) : [];
  if (!topMoves.length && data?.action) {
    topMoves = [
      {
        rank: 1,
        action: data.action,
        value: data.value ?? 0,
        visits: data.visits ?? 0,
        confidence: data.confidence ?? 100,
        label: "",
      },
    ];
  }
  topMoves.sort((a, b) => (b.confidence ?? 0) - (a.confidence ?? 0) || (b.visits ?? 0) - (a.visits ?? 0));
  topMoves.forEach((m, i) => {
    m.rank = i + 1;
  });
  selectedIdx = 0;
  lastAdvice = topMoves[0]?.action || null;

  if (!topMoves.length) {
    const empty = document.createElement("div");
    empty.className = "top-empty";
    empty.textContent = "No moves yet — click Find top 3.";
    list.appendChild(empty);
    return;
  }

  topMoves.forEach((m, i) => {
    const btn = document.createElement("button");
    btn.type = "button";
    btn.className = "top-item" + (i === selectedIdx ? " selected" : "");
    const explain = m.action?.explain || m.explain || "Move";
    const why = m.why || m.action?.why || "";
    const conf = Math.round(m.confidence ?? 0);
    const whyBit = why ? ` — ${why}` : "";
    btn.innerHTML = `
      <span class="rank">#${m.rank || i + 1}</span>
      <span class="body">
        <strong>${explain}</strong>
        <span class="sub"><span class="conf">${conf}% confidence</span> this is best${whyBit}</span>
      </span>`;
    btn.addEventListener("click", () => {
      selectedIdx = i;
      lastAdvice = m.action;
      [...list.querySelectorAll(".top-item")].forEach((el, j) => {
        el.classList.toggle("selected", j === i);
      });
      if (state) renderBoard(state);
      $("playRec").disabled = !lastAdvice || state?.game_over;
      $("meta").textContent = `Selected #${i + 1} · ${conf}% confidence it is the best move`;
    });
    list.appendChild(btn);
  });
}

function clearAdvice() {
  lastAdvice = null;
  topMoves = [];
  selectedIdx = 0;
  $("topList").innerHTML = "";
  $("visitTag").textContent = "Same position in prior games: —";
  $("meta").textContent = "";
}

function renderSide(s) {
  const you = s.you;
  const me = s.players[you];
  const hand = $("hand");
  hand.innerHTML = "";

  const res = me.res || [];
  let any = false;
  res.forEach((n, i) => {
    if (!n) return;
    any = true;
    const pill = document.createElement("span");
    pill.className = "pill";
    pill.textContent = `${RES[i]} × ${n}`;
    hand.appendChild(pill);
  });

  const DEV_LABELS = {
    knight: "Knight",
    vp: "Victory Point",
    monopoly: "Monopoly",
    year_of_plenty: "Invention",
    road_building: "Road Building",
  };
  const NEW_DEV = ["", "Knight", "Victory Point", "Monopoly", "Invention", "Road Building"];
  if (me.devs) {
    for (const [key, label] of Object.entries(DEV_LABELS)) {
      const n = me.devs[key] || 0;
      if (!n) continue;
      any = true;
      const pill = document.createElement("span");
      pill.className = "pill pill-dev";
      let text = `${label} × ${n}`;
      if (me.new_dev && NEW_DEV[me.new_dev] === label) text += " (new)";
      pill.textContent = text;
      hand.appendChild(pill);
    }
  }

  if (!any) {
    hand.innerHTML = `<span class="pill">empty</span>`;
  }

  $("phase").textContent = `Phase: ${s.phase.replace("_", " ")} · current: ${
    s.players[s.current].name
  }${s.last_roll ? ` · last roll ${s.last_roll}` : ""}`;

  const scores = $("scores");
  scores.innerHTML = "";
  for (const p of s.players) {
    const li = document.createElement("li");
    if (p.id === you) li.classList.add("you");
    const bits = [];
    if (p.building_vp) bits.push(`${p.building_vp} build`);
    if (p.longest_road) bits.push("+2 longest road");
    if (p.largest_army) bits.push("+2 largest army");
    if (p.id === you && p.vp_cards_count) bits.push(`+${p.vp_cards_count} VP cards`);
    else if (p.knights) bits.push(`${p.knights} knights`);
    li.innerHTML = `<span>${p.name}${p.id === you ? " (you)" : ""}</span>
      <span style="text-align:right"><strong>${p.vp} VP</strong>
      <div class="vp-break">${bits.join(" · ") || `${p.hand_size} cards`}</div></span>`;
    scores.appendChild(li);
  }

  const busy = autoRunning;
  $("think").disabled = s.game_over || busy;
  $("autoPlay").disabled = s.game_over || busy;
  $("playRec").disabled = !lastAdvice || s.game_over || busy;
  $("autoFull").disabled = s.game_over || busy;
  $("autoStop").disabled = !busy;

  if (s.game_over) {
    $("meta").textContent =
      s.winner === you ? "You won!" : `Game over — ${s.players[s.winner]?.name || "?"} wins`;
    showRecap(s);
  }
}

function hideRecap() {
  $("recapModal")?.classList.add("hidden");
}

function pieceCounts(s, playerId) {
  const settles = (s.vertices || []).filter((v) => v.owner === playerId && !v.city).length;
  const cities = (s.vertices || []).filter((v) => v.owner === playerId && v.city).length;
  const roads = (s.edges || []).filter((e) => e.owner === playerId).length;
  return { settles, cities, roads };
}

function buildRecap(s) {
  const you = s.you;
  const winner = s.winner;
  const rows = (s.players || [])
    .map((p) => {
      const pieces = pieceCounts(s, p.id);
      const awards = [];
      if (p.longest_road) awards.push("LR");
      if (p.largest_army) awards.push("LA");
      return { ...p, ...pieces, awards };
    })
    .sort((a, b) => b.vp - a.vp || a.id - b.id);

  const me = rows.find((p) => p.id === you) || rows[0];
  const win = rows.find((p) => p.id === winner) || rows[0];
  const why = [];
  const won = winner === you;

  if (won) {
    why.push(`You closed it at ${me.vp} VP.`);
    if (me.award_vp) why.push(`Awards contributed +${me.award_vp} (Longest Road / Largest Army).`);
    if (me.cities >= 2) why.push(`Cities carried a lot of your score (${me.cities} on the board).`);
  } else {
    why.push(`${win.name} finished at ${win.vp} VP; you ended on ${me.vp}.`);
    if ((win.award_vp || 0) >= 4) {
      why.push(`${win.name} held both Longest Road and Largest Army (+4).`);
    } else if (win.longest_road) {
      why.push(`${win.name} held Longest Road (+2).`);
    } else if (win.largest_army) {
      why.push(`${win.name} held Largest Army (+2).`);
    }
    if (me.settles + me.cities <= 2) {
      why.push(
        `Your board stalled at ${me.settles} settlement(s) and ${me.cities} city(ies) — needed more expansion.`
      );
    }
    if ((me.building_vp || 0) + 1 < (win.building_vp || 0)) {
      why.push(
        `${win.name} outbuilt you on structures (${win.building_vp} vs ${me.building_vp} building VP).`
      );
    }
    if ((me.vp_cards_count || 0) >= 2) {
      why.push(`You were holding ${me.vp_cards_count} hidden VP cards that never caught the awards race.`);
    }
    if ((me.knights || 0) >= 2 && !me.largest_army && win.largest_army) {
      why.push(`Knight race: you had ${me.knights}, ${win.name} had ${win.knights} and kept Largest Army.`);
    }
    if ((me.roads || 0) + 2 < (win.roads || 0) && win.longest_road) {
      why.push(`Road race: you had ${me.roads} roads vs ${win.name}'s ${win.roads}.`);
    }
  }

  if (!why.length) why.push("Final standings below.");
  return {
    won,
    headline: won ? "Victory" : `Defeat — ${win.name} wins`,
    why,
    rows,
  };
}

function showRecap(s) {
  if (!s?.game_over) return;
  const key = `${s.winner}-${(s.players || []).map((p) => p.vp).join(",")}`;
  if (key === lastRecapKey && !$("recapModal").classList.contains("hidden")) return;
  lastRecapKey = key;

  const recap = buildRecap(s);
  $("recapHeadline").textContent = recap.headline;
  $("recapHeadline").style.color = recap.won ? "var(--accent-2)" : "var(--ember)";

  const why = $("recapWhy");
  why.innerHTML = "";
  recap.why.forEach((line) => {
    const li = document.createElement("li");
    li.textContent = line;
    why.appendChild(li);
  });

  const tbody = $("recapTable").querySelector("tbody");
  tbody.innerHTML = "";
  recap.rows.forEach((p) => {
    const tr = document.createElement("tr");
    if (p.id === s.you) tr.classList.add("you");
    if (p.id === s.winner) tr.classList.add("winner");
    const board = `${p.settles}S / ${p.cities}C`;
    const awards = p.awards.length ? p.awards.join("+") : "—";
    const name = `${p.name}${p.id === s.you ? " (you)" : ""}`;
    tr.innerHTML = `<td>${name}</td><td><strong>${p.vp}</strong></td><td>${board}</td>
      <td>${p.roads}</td><td>${p.knights || 0}</td><td>${awards}</td>`;
    tbody.appendChild(tr);
  });

  $("recapModal").classList.remove("hidden");
}

function pushLog(msg) {
  const ul = $("log");
  const li = document.createElement("li");
  li.textContent = msg;
  ul.prepend(li);
}

function setState(s, priorGames) {
  state = s;
  if (priorGames != null) {
    const n = Number(priorGames);
    $("visitTag").textContent = `Prior games with this position: ${n}`;
    const known = s.positions_known;
    const mem = $("trainStatus");
    if (mem && !mem.classList.contains("busy")) {
      const posKnown = window.__positionsKnown;
      const knownTxt = posKnown != null ? `${posKnown} positions · ` : "";
      mem.textContent = `Memory: ${knownTxt}this position seen in ${n} games (drops as the position gets rarer)`;
    }
  }
  renderBoard(s);
  renderSide(s);
}

function showOpponentMoves(log) {
  const ul = $("oppLog");
  ul.innerHTML = "";
  if (!log || !log.length) {
    const li = document.createElement("li");
    li.className = "muted";
    li.textContent = "No opponent actions (already your turn).";
    ul.appendChild(li);
    return;
  }
  log.forEach((m) => {
    const li = document.createElement("li");
    li.textContent = m;
    ul.appendChild(li);
    pushLog(m);
  });
}

async function newGame() {
  stopAuto();
  clearAdvice();
  hideRecap();
  lastRecapKey = null;
  $("meta").textContent = "Starting…";
  const you = Number($("seat").value);
  const data = await api("/api/new", { you, seed: Date.now() % 100000 });
  if (data.positions_known != null) window.__positionsKnown = data.positions_known;
  setState(data.state, data.prior_games ?? 0);
  pushLog(`New game — you are ${data.state.players[you].name} (board from training pool)`);
  pushLog(
    `This position: ${data.prior_games ?? 0} prior games · ${
      data.positions_known ?? "?"
    } positions stored`
  );
  await autoOpponents(true);
  toast("Board ready · weaker bots (~80% favor for your color)");
}

async function think() {
  $("think").disabled = true;
  $("meta").textContent = "Searching for the fastest path to 10 VP…";
  try {
    const data = await api("/api/advise", { sims: 160 });
    const prior = data.prior_games ?? data.position_visits ?? 0;
    if (data.positions_known != null) window.__positionsKnown = data.positions_known;
    renderTopList(data);
    setState(data.state, prior);
    $("playRec").disabled = false;
    const best = topMoves[0];
    const conf = Math.round(best?.confidence ?? 0);
    $("meta").textContent = best
      ? `Top pick #1 · ${conf}% confidence — click another to select`
      : "";
    pushLog(`Advice: ${best?.action?.explain || data.action?.explain || "?"}`);
  } catch (e) {
    $("meta").textContent = e.message;
  } finally {
    $("think").disabled = autoRunning || state?.game_over;
  }
}

async function playRecommended() {
  const act = selectedAction();
  if (!act) return;
  const data = await api("/api/apply", { action: act });
  pushLog(`You: ${act.explain}`);
  clearAdvice();
  $("meta").textContent = "Your move is in. Watching opponents…";
  setState(data.state, data.prior_games);
  await autoOpponents(true);
}

async function autoOpponents(announce) {
  const data = await api("/api/auto");
  if (data.positions_known != null) window.__positionsKnown = data.positions_known;
  if (announce) showOpponentMoves(data.log || []);
  setState(data.state, data.prior_games);
  if (data.state.game_over) {
  } else if ((data.log || []).length) {
    $("meta").textContent = "Opponents finished — your turn. Find top 3.";
  } else {
    $("meta").textContent = "Your turn — find top 3.";
  }
  return data.state;
}

function stopAuto() {
  autoAbort = true;
  autoRunning = false;
  if (state) renderSide(state);
}

async function runAutoPlay() {
  if (autoRunning || !state || state.game_over) return;
  autoRunning = true;
  autoAbort = false;
  renderSide(state);
  toast("Autoplay on");
  try {
    while (!autoAbort && state && !state.game_over) {
      await think();
      if (autoAbort || !lastAdvice || state.game_over) break;
      await new Promise((r) => setTimeout(r, 1100));
      if (autoAbort) break;
      await playRecommended();
      if (autoAbort || !state || state.game_over) break;
      await new Promise((r) => setTimeout(r, 850));
    }
  } catch (e) {
    toast(e.message);
  } finally {
    autoRunning = false;
    autoAbort = false;
    if (state) renderSide(state);
    if (state?.game_over) toast("Autoplay finished");
    else $("meta").textContent = "Autoplay stopped.";
  }
}

let trainPoll = null;

async function refreshTrainStatus() {
  try {
    const res = await fetch("/api/train/status");
    const data = await res.json();
    const el = $("trainStatus");
    const btn = $("trainBtn");
    if (!el) return;
    if (data.running) {
      el.classList.add("busy");
      el.textContent = `Training… ${data.done}/${data.games} unique self-play games (learning weights)`;
      if (btn) btn.disabled = true;
    } else if (data.error) {
      el.classList.remove("busy");
      el.textContent = `Training error: ${data.error}`;
      if (btn) btn.disabled = false;
      if (trainPoll) {
        clearInterval(trainPoll);
        trainPoll = null;
      }
    } else if (data.last_result) {
      el.classList.remove("busy");
      el.textContent =
        `Last train: ${data.last_result.games} unique games done. Memory updated — New game loads weights + positions.`;
      if (btn) btn.disabled = false;
      if (trainPoll) {
        clearInterval(trainPoll);
        trainPoll = null;
      }
      api("/api/reload_visits")
        .then((r) => {
          if (r.positions_known != null) {
            el.textContent += ` Positions known: ${r.positions_known}.`;
          }
        })
        .catch(() => {});
    } else {
      el.classList.remove("busy");
      if (btn) btn.disabled = false;
    }
  } catch (_) {
  }
}

async function startTrain() {
  try {
    const data = await api("/api/train", { games: 1200 });
    toast(data.message || "Training started");
    $("trainStatus").classList.add("busy");
    $("trainStatus").textContent = data.message || "Training…";
    $("trainBtn").disabled = true;
    if (trainPoll) clearInterval(trainPoll);
    trainPoll = setInterval(refreshTrainStatus, 800);
    refreshTrainStatus();
  } catch (e) {
    toast(e.message);
  }
}

$("newGame").addEventListener("click", () => newGame().catch((e) => toast(e.message)));
$("think").addEventListener("click", () => think().catch((e) => toast(e.message)));
$("playRec").addEventListener("click", () => playRecommended().catch((e) => toast(e.message)));
$("autoPlay").addEventListener("click", () =>
  autoOpponents(true).catch((e) => toast(e.message))
);
$("autoFull").addEventListener("click", () => runAutoPlay().catch((e) => toast(e.message)));
$("autoStop").addEventListener("click", () => {
  stopAuto();
  toast("Stopping…");
});
$("trainBtn").addEventListener("click", () => startTrain());
$("recapClose")?.addEventListener("click", () => hideRecap());
$("recapNew")?.addEventListener("click", () => newGame().catch((e) => toast(e.message)));
$("recapModal")?.addEventListener("click", (e) => {
  if (e.target === $("recapModal")) hideRecap();
});

$("meta").textContent = "Click “New game” to begin.";
refreshTrainStatus();
