const COLORS = {
  hills: "#c47a4a",
  forest: "#2f7a4b",
  mountains: "#7a8794",
  fields: "#d4b84a",
  pasture: "#7bbf5a",
  desert: "#c9b896",
};
const PLAYER = ["#e4572e", "#f4f1ec", "#e89a12", "#3b82f6"];
const RES = ["brick", "lumber", "ore", "grain", "wool"];

let state = null;
let lastAdvice = null;
let topMoves = [];
let selectedIdx = 0;
let autoRunning = false;
let autoAbort = false;

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
    const a = (Math.PI / 180) * (60 * i - 30);
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
  bg.setAttribute("fill", "rgba(10, 70, 82, 0.35)");
  svg.appendChild(bg);

  // Hex tiles first
  for (const h of s.hexes) {
    const g = document.createElementNS(ns, "g");
    const poly = document.createElementNS(ns, "polygon");
    poly.setAttribute("points", hexPolygon(h.x, h.y, size - 1.5));
    poly.setAttribute("fill", COLORS[h.terrain] || "#888");
    poly.setAttribute("stroke", "rgba(8,20,24,0.55)");
    poly.setAttribute("stroke-width", "2");
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

  // Roads on edges (between corners)
  for (const e of s.edges) {
    if (e.owner < 0) continue;
    const line = document.createElementNS(ns, "line");
    line.setAttribute("x1", e.x0);
    line.setAttribute("y1", e.y0);
    line.setAttribute("x2", e.x1);
    line.setAttribute("y2", e.y1);
    line.setAttribute("stroke", PLAYER[e.owner]);
    line.setAttribute("stroke-width", "5.5");
    line.setAttribute("stroke-linecap", "round");
    line.setAttribute("opacity", e.owner === 1 ? "0.95" : "0.92");
    if (e.owner === 1) line.setAttribute("stroke", "#e8e4dc");
    svg.appendChild(line);
  }

  // Empty intersections (legal build spots are corners only)
  for (const v of s.vertices) {
    if (v.owner >= 0) continue;
    const c = document.createElementNS(ns, "circle");
    c.setAttribute("cx", v.x);
    c.setAttribute("cy", v.y);
    c.setAttribute("r", "3.2");
    c.setAttribute("fill", "rgba(255,255,255,0.22)");
    c.setAttribute("stroke", "rgba(8,20,24,0.35)");
    c.setAttribute("stroke-width", "1");
    svg.appendChild(c);
  }

  // Settlements / cities on corners only
  for (const v of s.vertices) {
    if (v.owner < 0) continue;
    if (v.city) {
      const rect = document.createElementNS(ns, "rect");
      rect.setAttribute("x", v.x - 8);
      rect.setAttribute("y", v.y - 8);
      rect.setAttribute("width", "16");
      rect.setAttribute("height", "16");
      rect.setAttribute("rx", "2");
      rect.setAttribute("fill", PLAYER[v.owner]);
      rect.setAttribute("stroke", "#0b1518");
      rect.setAttribute("stroke-width", "1.5");
      svg.appendChild(rect);
    } else {
      // House-like triangle on the intersection
      const poly = document.createElementNS(ns, "polygon");
      const x = v.x, y = v.y;
      poly.setAttribute(
        "points",
        `${x},${y - 9} ${x + 8},${y + 5} ${x - 8},${y + 5}`
      );
      poly.setAttribute("fill", PLAYER[v.owner]);
      poly.setAttribute("stroke", "#0b1518");
      poly.setAttribute("stroke-width", "1.5");
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
        label: data.strategy || "",
      },
    ];
  }
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
    const explain = m.action?.explain || m.explain || m.label || "Move";
    const conf = Math.round(m.confidence ?? 0);
    btn.innerHTML = `
      <span class="rank">#${m.rank || i + 1}</span>
      <span class="body">
        <strong>${explain}</strong>
        <span class="sub"><span class="conf">${conf}% confidence</span> this is best${
          m.label ? ` · ${m.label}` : ""
        }</span>
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
  $("strategyTag").textContent = "Strategy: —";
  $("visitTag").textContent = "Same position in prior games: —";
  $("meta").textContent = "";
}

function renderSide(s) {
  const you = s.you;
  const me = s.players[you];
  const hand = $("hand");
  hand.innerHTML = "";
  me.res.forEach((n, i) => {
    if (!n) return;
    const pill = document.createElement("span");
    pill.className = "pill";
    pill.textContent = `${RES[i]} × ${n}`;
    hand.appendChild(pill);
  });
  if (!me.res.some(Boolean)) {
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
    li.innerHTML = `<span>${p.name}${p.id === you ? " (you)" : ""}</span><span>${p.vp} VP · hand ${p.hand_size}</span>`;
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
  }
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
    $("visitTag").textContent = `Same position in prior games: ${priorGames}`;
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
  $("meta").textContent = "Starting…";
  const you = Number($("seat").value);
  const data = await api("/api/new", { you, seed: Date.now() % 100000 });
  setState(data.state, data.prior_games ?? 0);
  pushLog(`New game — you are ${data.state.players[you].name} (unique random board)`);
  pushLog(
    `This opening seen in ${data.prior_games ?? 0} prior games · memory ${data.positions_known ?? "?"} positions`
  );
  if (data.positions_known != null) {
    $("trainStatus").textContent = `Position memory: ${data.positions_known} unique · ${
      data.position_hits ?? 0
    } total hits (grows when you Train)`;
  }
  await autoOpponents(true);
  toast("Board ready · opponents ~20–35% imperfect");
}

async function think() {
  $("think").disabled = true;
  $("meta").textContent = "Searching for the fastest path to 10 VP…";
  try {
    const data = await api("/api/advise", { sims: 400 });
    if (data.strategy) $("strategyTag").textContent = `Strategy: ${data.strategy}`;
    const prior = data.prior_games ?? data.position_visits ?? 0;
    $("visitTag").textContent = `Same position in prior games: ${prior}`;
    renderTopList(data);
    setState(data.state, prior);
    renderBoard(data.state);
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
  if (announce) showOpponentMoves(data.log || []);
  setState(data.state, data.prior_games);
  if (data.state.game_over) {
    // renderSide handles winner text
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
  toast("Slow autoplay on");
  try {
    while (!autoAbort && state && !state.game_over) {
      await think();
      if (autoAbort || !lastAdvice || state.game_over) break;
      // Pause so you can read the top 3 + confidence
      await new Promise((r) => setTimeout(r, 2200));
      if (autoAbort) break;
      await playRecommended();
      if (autoAbort || !state || state.game_over) break;
      await new Promise((r) => setTimeout(r, 1800));
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
      // Pull fresh position memory into the live engine
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
    /* ignore */
  }
}

async function startTrain() {
  try {
    const data = await api("/api/train", { games: 1000 });
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

$("meta").textContent = "Click “New game” to begin.";
refreshTrainStatus();
