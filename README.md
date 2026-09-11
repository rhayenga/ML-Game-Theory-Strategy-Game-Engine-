# Catan Coach

Local coaching engine for **Settlers of Catan**: a C++20 rules + strategy engine, MCTS move advice, PyTorch value-net training from self-play, and a browser UI to play and train on your machine.

> Unofficial fan / research project — not affiliated with Catan Studio or Asmodee.

**Project page:** [rhayenga.github.io/ML-Game-Theory-Strategy-Game-Engine](https://rhayenga.github.io/ML-Game-Theory-Strategy-Game-Engine/)  
**Repo:** [github.com/rhayenga/ML-Game-Theory-Strategy-Game-Engine](https://github.com/rhayenga/ML-Game-Theory-Strategy-Game-Engine)

## What it does

- Full legal-move engine (settlements, cities, roads, robber, harbors / maritime trade, development cards)
- Heuristic evaluation plus strategy scoring (expansion, awards, trades, harbors)
- **Find top 3** — MCTS search for your seat; softer one-ply bots for opponents
- **Train** — C++ self-play dumps position features; PyTorch fits a value net and writes `build/eval_weights.json` (plus visit memory for openings)
- End-of-game **recap** (VP, board pieces, narrative of what swung the match)

Playing and advising use the C++ bridge. Training **requires** PyTorch.

## Requirements

- macOS or Linux
- C++20 compiler (`clang++` / compatible)
- Python 3
- PyTorch (installed automatically by `make ui` into `.venv`)

## Quick start

```bash
git clone https://github.com/rhayenga/ML-Game-Theory-Strategy-Game-Engine.git
cd ML-Game-Theory-Strategy-Game-Engine
make -j4
make ui
```

`make ui` builds the bridge if needed, creates `.venv`, installs `ml/requirements.txt`, frees port **8765** if something else is bound, and starts the server.

Open `http://127.0.0.1:8765/` in your browser (local server only).

Manual start (after `make -j4` and a venv with PyTorch):

```bash
source .venv/bin/activate
PYTHONUNBUFFERED=1 python3 web/server.py
```

## Controls

| Action | What it does |
|--------|----------------|
| **New game** | Board from the training pool; pick your color |
| **Find top 3** | MCTS recommendations for your seat |
| **Play selected** | Apply a recommended move, then opponents act |
| **Autoplay** | Follow the top advice until the game ends |
| **Train 1200** | Self-play → PyTorch value-net update |

After Train finishes, start a **New game** (or reload) so the bridge loads the new weights and visit memory.

## Layout

```
apps/          CLI: bridge, train, advise, bench
include/catan/ Public headers
src/           Rules, eval, MCTS, strategy, visits, self-play
ml/            PyTorch value-net trainer (required for Train)
web/           Local UI (server.py, app.js, index.html, style.css)
docs/          GitHub Pages project site
build/         Binaries, eval_weights.json, samples, checkpoints
```

## Training pipeline

1. `catan_train` plays games (samples + visit priors; does not own weight updates when `--lr 0`)
2. `ml/train_value.py` trains a small MLP, exports sanitized linear weights to `build/eval_weights.json`, saves `build/value_net.pt`
3. The UI / bridge loads those weights for eval and MCTS

CLI equivalent:

```bash
./build/catan_train 200 --lr 0 --samples build/train_samples.jsonl
python3 ml/train_value.py
```

## Notes

- Large `build/position_visits.json`, `train_samples.jsonl`, and `value_net.pt` are gitignored — regenerate locally with Train
- `build/eval_weights.json` may be kept as a starting point for eval

## License

MIT — see [LICENSE](LICENSE).
