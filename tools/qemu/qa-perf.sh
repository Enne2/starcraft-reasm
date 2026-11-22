#!/usr/bin/env bash
# QA sampler for one host/guest-visible Linux PID plus SC_FPS_LOG parser.
# Usage:
#   qa-perf.sh sample PID [SECONDS=60] [INTERVAL=1] [CSV=stdout]
#   qa-perf.sh parse GAME.LOG
set -euo pipefail

usage() { echo "Usage: $0 sample PID [SECONDS=60] [INTERVAL=1] [CSV=stdout] | parse GAME.LOG" >&2; exit 2; }
[[ $# -ge 2 ]] || usage
mode=$1; shift
case "$mode" in
  sample)
    [[ $# -ge 1 && $1 =~ ^[0-9]+$ ]] || usage
    pid=$1; duration=${2:-60}; interval=${3:-1}; out=${4:--}
    [[ $duration =~ ^[0-9]+([.][0-9]+)?$ && $interval =~ ^[0-9]+([.][0-9]+)?$ ]] || usage
    [[ -r /proc/$pid/stat ]] || { echo "PID $pid non leggibile in /proc" >&2; exit 1; }
    hz=$(getconf CLK_TCK); page_size=$(getconf PAGESIZE)
    read_stat() {
      local line rest
      IFS= read -r line < "/proc/$pid/stat" || return 1
      # comm may contain spaces and ')'; strip through the last ') ' (field 2).
      rest=${line##*) }
      read -r -a fields <<< "$rest"
      # remainder starts at field 3: utime=14 (index 11), stime=15 (12), rss=24 (21).
      [[ ${#fields[@]} -ge 22 ]] || return 1
      ticks=$((fields[11] + fields[12]))
      rss_kib=$((fields[21] * page_size / 1024))
    }
    if [[ $out == - ]]; then exec 3>&1; else exec 3>"$out"; fi
    printf 'epoch,pid,cpu_pct_one_core,rss_kib\n' >&3
    read_stat || { echo "PID $pid terminato prima del campionamento" >&2; exit 1; }
    prev_ticks=$ticks
    start=$SECONDS
    end=$(awk -v s="$start" -v d="$duration" 'BEGIN { printf "%.3f", s+d }')
    while awk -v n="$SECONDS" -v e="$end" 'BEGIN { exit !(n<e) }'; do
      sleep "$interval"
      read_stat || break
      now=$SECONDS
      elapsed=$(awk -v n="$now" -v p="$start" 'BEGIN { print n-p }')
      cpu=$(awk -v t="$ticks" -v p="$prev_ticks" -v dt="$elapsed" -v h="$hz" 'BEGIN { if (dt>0) printf "%.2f", 100*(t-p)/h/dt; else print "0.00" }')
      printf '%s,%s,%s,%s\n' "$(date +%s)" "$pid" "$cpu" "$rss_kib" >&3
      prev_ticks=$ticks; start=$now
    done
    exec 3>&-
    ;;
  parse)
    [[ $# -eq 1 && -r $1 ]] || usage
    python3 - "$1" <<'PY'
import math, re, statistics, sys
from pathlib import Path
# Format emitted by SC_FPS_LOG=1: [fps] N fps, present medio X ms, ...
rx = re.compile(r"\[fps\]\s+(?:GPU\s+)?([0-9]+(?:\.[0-9]+)?)\s+fps(?:,\s+present medio\s+([0-9]+(?:\.[0-9]+)?)\s+ms)?")
rows = [(float(m.group(1)), float(m.group(2)) if m.group(2) is not None else None)
        for line in Path(sys.argv[1]).read_text(errors="replace").splitlines() if (m := rx.search(line))]
if not rows:
    print("samples=0 (nessuna riga SC_FPS_LOG riconosciuta)")
    raise SystemExit(1)
def p95(values):
    s = sorted(values)
    return s[max(0, math.ceil(.95 * len(s)) - 1)]
def fmt(x): return f"{x:.3f}"
fps = [row[0] for row in rows]
ms = [row[1] for row in rows if row[1] is not None]
print(f"samples={len(rows)} fps_median={fmt(statistics.median(fps))} fps_p95={fmt(p95(fps))} fps_min={fmt(min(fps))} fps_max={fmt(max(fps))}")
if ms:
    print(f"present_ms_samples={len(ms)} present_ms_median={fmt(statistics.median(ms))} present_ms_p95={fmt(p95(ms))} present_ms_min={fmt(min(ms))} present_ms_max={fmt(max(ms))}")
else:
    print("present_ms_samples=0 (log SC_FPS_LOG privo di present medio)")
PY
    ;;
  *) usage ;;
esac
