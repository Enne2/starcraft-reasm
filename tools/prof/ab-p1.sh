#!/bin/bash
# ab-p1.sh — A/B controllato baseline (pre-P1) vs P1, sotto carico CPU, a parità di memoria.
#
# Il carico è generato con N processi `yes` (nessuna dipendenza esterna, memoria trascurabile):
#   - load-on : N worker lasciati girare sulla macchina mentre il gioco gira (contesa reale)
#   - load-off: nessun worker (controllo)
# Ogni serie: 3 run da 40 s, sandbox Xvfb + audio dummy, stesso scenario (gameplay, cap 60), stesse
# porte/IP. Vengono riportati p50/p95/p99, jitter, alloc/frame, RSS e CPU% del processo.
set -u
ROOT=/home/enne2/dev/starcraft-reasm-generic
BASE=/tmp/base-pre/build/starcraft-x86
P1=$ROOT/build/starcraft-x86
NLOAD=${NLOAD:-6}
OUT=$ROOT/build/prof/ab-p1
mkdir -p "$OUT"

free_mb() { free -m | awk 'NR==2{print $3}'; }

run_series() {           # $1=tag  $2=binario  $3=load(on|off)  $4=run
  local tag=$1 bin=$2 load=$3 n=$4
  local tagdir="$OUT/$tag/run-$n"; mkdir -p "$tagdir"
  local pids=()
  if [ "$load" = "on" ]; then
    for i in $(seq 1 "$NLOAD"); do yes > /dev/null & pids+=($!); done
    sleep 2
  fi
  SC_PRESENT=cpu SC_FPS_CAP=60 SC_PRESENT_MIN_MS=0 SC_FRAME_LOG="$tagdir/frames.txt" \
  SC_BIN="$bin" "$ROOT/tools/run-isolated.sh" start --id "ab-$tag-$n" --skip-intro 1 --smack 1 >"$tagdir/start.txt" 2>&1
  local pid log sock
  pid=$(sed -nE 's/.*game_pid=([0-9]+).*/\1/p' "$tagdir/start.txt")
  log=$(sed -nE 's/.*log=(.*)\/game.log.*/\1/p' "$tagdir/start.txt")
  sock="$log/sandbox/sc-ctl.sock"
  sleep 6
  # scenario identico: Single Player -> Original -> profilo -> Terran -> Rebel Yell -> Boot Camp -> Start
  press() { python3 "$ROOT/tools/scctl" --sock "$sock" move "$1" "$2" >/dev/null 2>&1; sleep 0.3
            python3 "$ROOT/tools/scctl" --sock "$sock" down "$1" "$2" >/dev/null 2>&1; sleep 0.25
            python3 "$ROOT/tools/scctl" --sock "$sock" up "$1" "$2" >/dev/null 2>&1; sleep "${3:-1.2}"; }
  press 220 120 2; press 262 214 2.5; press 100 109 1.5; press 517 391 3
  press 320 140 3; press 320 440 2; press 320 440 2.5; press 538 390 4; press 197 262 1
  # campionamento memoria+CPU per 40 s
  python3 - "$pid" 40 "$tagdir/rss.txt" <<'PY'
import sys, pathlib, subprocess
pid, secs, out = sys.argv[1], float(sys.argv[2]), sys.argv[3]
spec = pathlib.Path("/home/enne2/dev/starcraft-reasm-generic/tools/prof/prof-run.py")
import importlib.util as u
m = u.module_from_spec(u.spec_from_file_location("p", spec)); u.spec_from_file_location("p", spec).loader.exec_module(m)
m.sample_rss(int(pid), secs, out)
PY
  "$ROOT/tools/run-isolated.sh" stop --id "ab-$tag-$n" >/dev/null 2>&1
  for p in "${pids[@]:-}"; do kill "$p" 2>/dev/null; done
  wait 2>/dev/null
  python3 - "$tagdir" <<'PY'
import sys, json, pathlib, statistics
d = pathlib.Path(sys.argv[1])
v = [int(x) for x in (d/"frames.txt").read_text().split()] if (d/"frames.txt").exists() else []
acc = 0.0; keep = []
for x in v:
    acc += x/1e6
    if acc >= 3.0: keep.append(x)
if len(keep) < 100: keep = v[-max(1,len(v)//2):]
s = sorted(keep)
q = lambda f: s[min(len(s)-1, int(f*(len(s)-1)))] if s else 0
st = {"frames": len(s), "p50": q(.5), "p95": q(.95), "p99": q(.99)}
st["jitter_cv"] = round(statistics.pstdev(s)/q(.5), 3) if s and q(.5) else None
rows = [l.split() for l in (d/"rss.txt").read_text().splitlines()[1:] if l.strip()] if (d/"rss.txt").exists() else []
if rows:
    hdr = ["t","rss","hwm","swap","anon","file","shmem","pss","uss","minflt","majflt","cpu"]
    rr = [dict(zip(hdr, r)) for r in rows]
    st["rss_med"] = round(statistics.median([int(x["rss"]) for x in rr])/1024, 1)
    st["rss_peak"] = round(max(int(x["hwm"]) for x in rr)/1024, 1)
    st["cpu_med"] = round(statistics.median([int(x["cpu"]) for x in rr]), 1)
(d/"summary.json").write_text(json.dumps(st, indent=2))
print(f"    {d.parent.name} run{ d.name[-1] }: p50={st['p50']/1000:.2f}ms p95={st['p95']/1000:.2f} "
      f"p99={st['p99']/1000:.2f} jit={st['jitter_cv']} rss={st.get('rss_med','?')}/{st.get('rss_peak','?')} "
      f"cpu={st.get('cpu_med','?')}%")
PY
}

echo "== memoria prima: $(free_mb) MiB usati, swap $(free -m | awk 'NR==3{print $3}') MiB =="
for bin_tag in "baseline:$BASE" "p1:$P1"; do
  tag=${bin_tag%%:*}; bin=${bin_tag##*:}
  for load in off on; do
    echo "-- $tag / carico $load (worker=$NLOAD) --"
    for n in 1 2 3; do run_series "$tag-load-$load" "$bin" "$load" "$n"; done
  done
done
echo "== memoria dopo: $(free_mb) MiB usati =="
