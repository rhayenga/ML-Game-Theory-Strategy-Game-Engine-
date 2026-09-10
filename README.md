# Catan Coach

A Settlers of Catan coaching engine: C++20 rules + eval + MCTS, with a local web UI that recommends top moves and can autoplay against opponent bots.

> Unofficial fan / research project — not affiliated with Catan Studio or Asmodee.

## Features

- Legal-move engine with settlements, cities, roads, robber, maritime trade, and development cards
- Heuristic evaluation + strategy bonuses (expand, awards, trades)
- MCTS advice for your seat; imperfect one-ply opponents
- Self-play training (C++ + optional PyTorch value-net export)
- **Game recap** on finish — VP breakdown, board pieces, and why the game swung

## Requirements

- macOS or Linux
- `clang++` (C++20) or compatible compiler
- Python 3
- Optional: PyTorch (`pip install -r ml/requirements.txt`) for value-net fitting after self-play

## Quick start

**Project page:** [https://rhayenga.github.io/ML-Game-Theory-Strategy-Game-Engine-/](https://rhayenga.github.io/ML-Game-Theory-Strategy-Game-Engine-/)

The playable coach runs on your machine (GitHub Pages is the project site, not the game server):

```bash
make -j4
PYTHONUNBUFFERED=1 python3 web/server.py
```

Then open `http://127.0.0.1:8765/` in your browser.

Optional: `make ui` builds the bridge and starts the server.

### Optional PyTorch training

Play/advise never depends on PyTorch. If torch is installed, **Train** runs C++ self-play, then `ml/train_value.py` exports `build/eval_weights.json` (engine sanitizes weights on load). Without torch, C++ weights alone are kept.

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -r ml/requirements.txt
./build/catan_train 200   # dumps build/train_samples.jsonl
python3 ml/train_value.py
```

If you start the UI with the venv active, **Train** will run the PyTorch step automatically after self-play.

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
ml/            Optional PyTorch value-net trainer
web/           UI (server.py, app.js, index.html)
build/         Objects, binaries, optional weights JSON
```

## Notes
- `build/eval_weights.json` can be committed as a starting point; large `position_visits.json` / sample dumps are gitignored — regenerate via Train if you want memory.

## License

MIT — see [LICENSE](LICENSE).
