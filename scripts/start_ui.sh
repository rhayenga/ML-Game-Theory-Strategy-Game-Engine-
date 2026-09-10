#!/usr/bin/env bash
# Create .venv with PyTorch if needed, then start the local coach UI.

set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

if [[ ! -x "$ROOT/build/catan_bridge" ]]; then
  echo "Building engine…"
  make -j4
fi

if [[ ! -x "$ROOT/.venv/bin/python" ]]; then
  echo "Creating .venv…"
  python3 -m venv "$ROOT/.venv"
fi

echo "Ensuring PyTorch is installed…"
"$ROOT/.venv/bin/pip" install -q -r "$ROOT/ml/requirements.txt"

if lsof -nP -iTCP:8765 -sTCP:LISTEN >/dev/null 2>&1; then
  echo "Port 8765 is busy — stopping the old server…"
  # shellcheck disable=SC2046
  kill $(lsof -t -nP -iTCP:8765 -sTCP:LISTEN) 2>/dev/null || true
  sleep 0.5
fi

echo "Starting UI with PyTorch enabled → http://127.0.0.1:8765/"
exec env PYTHONUNBUFFERED=1 "$ROOT/.venv/bin/python" "$ROOT/web/server.py"
