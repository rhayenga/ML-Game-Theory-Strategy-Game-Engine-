# Catan Coach

A Settlers of Catan coaching engine: C++20 rules + eval + MCTS, with a local web UI that recommends top moves and can autoplay against opponent bots.

> Unofficial fan / research project — not affiliated with Catan Studio or Asmodee.

## Features

- Legal-move engine with settlements, cities, roads, robber, maritime trade, and development cards
- Heuristic evaluation + strategy bonuses (expand, awards, trades)
- MCTS advice for your seat; imperfect one-ply opponents
- Self-play training hooks (visit priors / eval weights)
- **Game recap** on finish — VP breakdown, board pieces, and why the game swung

## Requirements

- macOS or Linux
- `clang++` (C++20) or compatible compiler
- Python 3

## Quick start

This is a **local** app (not a hosted site). GitHub only stores the code — there is nothing to open in the repo “Website” field.

```bash
make -j4
PYTHONUNBUFFERED=1 python3 web/server.py
```

Then in your browser open: `http://127.0.0.1:8765/`

Optional: `make ui` builds the bridge and starts the server.

## Controls

| Action | What it does |
|--------|----------------|
| **New game** | Random board from the training pool; pick your color |
| **Find top 3** | MCTS search for your seat |
| **Play selected** | Apply a recommended move, then opponents act |
| **Autoplay** | Follow #1 advice until the game ends |
| **Train 1200** | Offline self-play to refresh weights / visit memory |

## Layout

```
apps/          CLI bridge, train, advise, bench
include/catan/ Public headers
src/           Engine, eval, MCTS, strategy, visits
web/           UI (server.py, app.js, index.html)
build/         Objects, binaries, optional weights JSON
```

## Notes
- `build/eval_weights.json` can be committed as a starting point; large `position_visits.json` is gitignored — regenerate via Train if you want memory.

## License

MIT — see [LICENSE](LICENSE).
