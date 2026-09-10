#!/bin/bash
set -euo pipefail
cd /Users/rowanhayenga/Desktop/Catan

# Kill listener on 8765
PIDS=$(lsof -nP -tiTCP:8765 -sTCP:LISTEN || true)
if [ -n "${PIDS}" ]; then
  kill -9 ${PIDS} || true
  sleep 0.5
fi
lsof -nP -iTCP:8765 -sTCP:LISTEN && echo "PORT_STILL_BUSY" && exit 1 || echo "PORT_FREE"

# Start UI
python3 web/server.py > build/ui.log 2>&1 &
UI_PID=$!
echo "UI_PID=${UI_PID}"

# Wait for ready
for i in $(seq 1 40); do
  if grep -q "Catan UI" build/ui.log 2>/dev/null; then
    echo "UI_READY"
    break
  fi
  sleep 0.25
done
grep -q "Catan UI" build/ui.log || { echo "UI_FAILED"; cat build/ui.log; exit 1; }

# Verify APIs
NEW_JSON=$(curl -sS -X POST http://127.0.0.1:8765/api/new -H 'Content-Type: application/json' -d '{"you":0,"seed":7}')
echo "NEW_JSON=${NEW_JSON}"
ADVISE_JSON=$(curl -sS -X POST http://127.0.0.1:8765/api/advise -H 'Content-Type: application/json' -d '{"sims":120}')
echo "ADVISE_KEYS=$(python3 -c "import json,sys; d=json.loads(sys.argv[1]); print('top' in d, 'position_visits' in d, d.get('opp_subopt'), list(d)[:20])" "$ADVISE_JSON")"
python3 - <<'PY' "$NEW_JSON" "$ADVISE_JSON"
import json,sys
new=json.loads(sys.argv[1])
adv=json.loads(sys.argv[2])
opp=new.get("opp_subopt")
print(f"opp_subopt={opp}")
ok_opp = isinstance(opp,(int,float)) and 0.2 <= float(opp) <= 0.35
print(f"opp_subopt_ok={ok_opp}")
print(f"has_top={'top' in adv}")
print(f"has_position_visits={'position_visits' in adv}")
print(f"top_len={len(adv.get('top', [])) if isinstance(adv.get('top'), list) else 'n/a'}")
PY

# Start training
./build/catan_train 7500 --epsilon 0.1 --lr 0.05 --seed 99 > build/train_7k5.log 2>&1 &
TRAIN_PID=$!
echo "TRAIN_PID=${TRAIN_PID}"
echo DONE
