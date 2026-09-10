# Catan Coach

A Settlers of Catan coaching engine: C++20 rules + eval + MCTS, with a local web UI that recommends top moves and can autoplay against opponent bots.

> Unofficial fan / research project — not affiliated with Catan Studio or Asmodee.

## Features

- Legal-move engine with settlements, cities, roads, robber, maritime trade, and development cards
- Heuristic evaluation + strategy bonuses (expand, awards, trades)
- MCTS advice for your seat; imperfect one-ply opponents
- Self-play training via PyTorch value-net (C++ generates games; PyTorch updates weights)
- **Game recap** on finish — VP breakdown, board pieces, and why the game swung

## Requirements

- macOS or Linux
- `clang++` (C++20) or compatible compiler
- Python 3 with PyTorch (`make ui` installs it into `.venv`)

## Quick start

**Project page:** [https://rhayenga.github.io/ML-Game-Theory-Strategy-Game-Engine-/](https://rhayenga.github.io/ML-Game-Theory-Strategy-Game-Engine-/)

Use this folder on your Desktop (`Desktop/Catan`), not an older clone under your home directory.

```bash
cd ~/Desktop/Catan
make -j4
make ui
```

That creates `.venv`, installs PyTorch, frees port 8765 if needed, and starts the UI. Open `http://127.0.0.1:8765/`.

**Train** runs C++ self-play to collect features, then **always** fits the PyTorch value net and writes `build/eval_weights.json`.

```bash
cd ~/Desktop/Catan
python3 -m venv .venv
source .venv/bin/activate
pip install -r ml/requirements.txt
PYTHONUNBUFFERED=1 python3 web/server.py
```

## Controls

| Action | What it does |
|--------|----------------|
| **New game** | Random board from the training pool; pick your color |
| **Find top 3** | MCTS search for your seat |
| **Play selected** | Apply a recommended move, then opponents act |
| **Autoplay** | Follow #1 advice until the game ends |
| **Train 1200** | Self-play + PyTorch value-net update |

## Layout

```
apps/          CLI bridge, train, advise, bench
include/catan/ Public headers
src/           Engine, eval, MCTS, strategy, visits
ml/            Optional PyTorch value-net trainer
web/           UI (server.py, app.js, index.html)
build/         Objects, binaries, optional weights JSON
```

## Notes
- `build/eval_weights.json` can be committed as a starting point; large `position_visits.json` / sample dumps are gitignored — regenerate via Train if you want memory.

## License

MIT — see [LICENSE](LICENSE).
