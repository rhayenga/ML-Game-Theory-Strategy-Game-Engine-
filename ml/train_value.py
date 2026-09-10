#!/usr/bin/env python3
# Train a PyTorch value net on self-play features; export eval_weights.json for the C++ engine.

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import torch
import torch.nn as nn
from torch.utils.data import DataLoader, TensorDataset

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SAMPLES = ROOT / "build" / "train_samples.jsonl"
DEFAULT_WEIGHTS = ROOT / "build" / "eval_weights.json"
DEFAULT_CKPT = ROOT / "build" / "value_net.pt"
FEAT_DIM = 6


class ValueNet(nn.Module):
    def __init__(self) -> None:
        super().__init__()
        self.net = nn.Sequential(
            nn.Linear(FEAT_DIM, 32),
            nn.ReLU(),
            nn.Linear(32, 16),
            nn.ReLU(),
            nn.Linear(16, 1),
        )

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        return self.net(x).squeeze(-1)


def load_samples(path: Path) -> tuple[torch.Tensor, torch.Tensor]:
    xs: list[list[float]] = []
    ys: list[float] = []
    with path.open() as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            row = json.loads(line)
            feat = row.get("feat")
            if not feat or len(feat) != FEAT_DIM:
                continue
            xs.append([float(v) for v in feat])
            ys.append(float(row.get("y", 0)))
    if not xs:
        raise SystemExit(f"no usable samples in {path}")
    return torch.tensor(xs, dtype=torch.float32), torch.tensor(ys, dtype=torch.float32)


def sanitize_race_weights(w: list[float], scale: float) -> tuple[list[float], float]:
    # Mirror src/eval.cpp sanitize_race_weights so exports stay playable.
    w = [max(0.0, min(12.0, float(x))) for x in w]
    w[0] = max(4.5, min(12.0, w[0]))
    w[1] = max(1.6, min(w[0] * 0.95, w[1]))
    w[2] = max(0.25, min(w[0] * 0.35, w[2]))
    w[3] = max(0.35, min(w[0] * 0.45, w[3]))
    w[4] = max(0.75, min(w[0] * 0.4, w[4]))
    w[5] = max(0.4, min(2.5, w[5]))
    scale = max(2.0, min(12.0, float(scale)))
    return w, scale


def load_prior_weights(path: Path) -> list[float] | None:
    if not path.exists():
        return None
    try:
        data = json.loads(path.read_text())
        w = data.get("w")
        if isinstance(w, list) and len(w) == FEAT_DIM:
            return [float(x) for x in w]
    except (OSError, json.JSONDecodeError, TypeError, ValueError):
        return None
    return None


def fit_linear_export(x: torch.Tensor, y: torch.Tensor, epochs: int, lr: float) -> list[float]:
    # Linear head matches the C++ eval: score = sum(w_i * f_i).
    model = nn.Linear(FEAT_DIM, 1, bias=True)
    opt = torch.optim.Adam(model.parameters(), lr=lr)
    loss_fn = nn.BCEWithLogitsLoss()
    ds = TensorDataset(x, y)
    loader = DataLoader(ds, batch_size=min(512, len(x)), shuffle=True)
    model.train()
    for _ in range(epochs):
        for xb, yb in loader:
            opt.zero_grad()
            pred = model(xb).squeeze(-1)
            loss = loss_fn(pred, yb)
            loss.backward()
            opt.step()
    with torch.no_grad():
        w = model.weight.squeeze(0).abs().clamp(min=0.05).tolist()
    return [float(v) for v in w]


def export_weights(w: list[float], out_path: Path, scale: float, blend: float = 0.2) -> None:
    prior = load_prior_weights(out_path)
    if prior is not None:
        w = [(1.0 - blend) * p + blend * n for p, n in zip(prior, w)]
    w, scale = sanitize_race_weights(w, scale)
    payload = {"w": w, "scale": scale, "source": "pytorch"}
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(payload, indent=2) + "\n")
    print(f"wrote {out_path}: w={w} scale={scale}")


def train(args: argparse.Namespace) -> None:
    x, y = load_samples(Path(args.samples))
    print(f"loaded {len(x)} samples from {args.samples}")

    ds = TensorDataset(x, y)
    loader = DataLoader(ds, batch_size=args.batch_size, shuffle=True)

    model = ValueNet()
    opt = torch.optim.Adam(model.parameters(), lr=args.lr)
    loss_fn = nn.BCEWithLogitsLoss()

    model.train()
    for epoch in range(args.epochs):
        total = 0.0
        n = 0
        for xb, yb in loader:
            opt.zero_grad()
            pred = model(xb)
            loss = loss_fn(pred, yb)
            loss.backward()
            opt.step()
            total += float(loss.item()) * len(xb)
            n += len(xb)
        print(f"epoch {epoch + 1}/{args.epochs} loss={total / max(1, n):.4f}")

    ckpt = Path(args.checkpoint)
    ckpt.parent.mkdir(parents=True, exist_ok=True)
    torch.save({"model": model.state_dict(), "feat_dim": FEAT_DIM}, ckpt)
    print(f"saved checkpoint {ckpt}")

    # Small sample dumps are noisy; keep C++ weights so short Train clicks stay safe.
    if len(x) < 5000 and not args.force_export:
        print(f"only {len(x)} samples (<5000) — keeping existing eval_weights.json (use --force-export to override)")
        return

    w = fit_linear_export(x, y, epochs=max(4, args.epochs // 2), lr=args.lr)
    export_weights(w, Path(args.weights), scale=args.scale)


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--samples", default=str(DEFAULT_SAMPLES))
    p.add_argument("--weights", default=str(DEFAULT_WEIGHTS))
    p.add_argument("--checkpoint", default=str(DEFAULT_CKPT))
    p.add_argument("--epochs", type=int, default=8)
    p.add_argument("--batch-size", type=int, default=256)
    p.add_argument("--lr", type=float, default=1e-3)
    p.add_argument("--scale", type=float, default=5.0)
    p.add_argument(
        "--force-export",
        action="store_true",
        help="Overwrite eval_weights.json even with <5000 samples",
    )
    args = p.parse_args()
    if not Path(args.samples).exists():
        print(f"missing samples file: {args.samples}", file=sys.stderr)
        print("Run self-play first (UI Train or ./build/catan_train).", file=sys.stderr)
        raise SystemExit(1)
    train(args)


if __name__ == "__main__":
    main()
