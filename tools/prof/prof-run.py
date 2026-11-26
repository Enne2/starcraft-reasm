#!/usr/bin/env python3
"""prof-run.py — baseline measurement harness for the native port.

One run = one isolated instance (Xvfb + dummy audio) for N seconds on a fixed scenario, collecting:
  * per-frame times (SC_FRAME_LOG, written by the runtime at every present)
  * frame rate per second and present cost (SC_FPS_LOG)
  * RSS of the game process sampled every second
  * optional allocation churn (LD_PRELOAD=count-alloc.so, SC_ALLOC_LOG)
  * optional `perf stat` counters for the whole run

Scenarios (deterministic, no input injection, no game asset modification):
  menu      : sits on the animated main menu (SC_SKIP_INTRO=1, no input)
  intro     : plays the two intro movies (SC_SKIP_INTRO=0)
  gameplay  : menu -> Single Player -> Original -> profile -> Terran -> map, via sc_ctl

Usage:
  tools/prof/prof-run.py --scenario menu --seconds 60 --repeat 3 --tag base-cpu
  tools/prof/prof-run.py --scenario gameplay --present gl --perf --alloc --tag base-gl

Output: build/prof/<tag>/run-<n>/{frames.txt,fps.txt,rss.txt,alloc.txt,perf.txt,summary.json}
and an aggregated build/prof/<tag>/aggregate.json printed as a table.
"""
import argparse, json, os, shutil, statistics, subprocess, sys, time, pathlib

ROOT = pathlib.Path(__file__).resolve().parents[2]
PROF = ROOT / "build" / "prof"
SCCTL = ROOT / "tools" / "scctl"
ALLOC_SO = PROF / "count-alloc.so"
WARMUP_S = 3.0          # frames before this are discarded (startup/first-map-load spikes)


def sh(cmd, **kw):
    return subprocess.run(cmd, shell=isinstance(cmd, str), capture_output=True, text=True, **kw)


def pct(sorted_vals, q):
    if not sorted_vals:
        return 0.0
    i = min(len(sorted_vals) - 1, max(0, int(round(q * (len(sorted_vals) - 1)))))
    return sorted_vals[i]


def stats_from_frames(path, warmup_s):
    vals = []
    try:
        raw = [int(x) for x in pathlib.Path(path).read_text().split()]
    except Exception:
        return None
    # drop the warmup window: the first frames include window creation and first map load
    acc = 0.0
    for v in raw:
        acc += v / 1e6
        if acc >= warmup_s:
            vals.append(v)
    if len(vals) < 100:
        vals = raw[-max(1, len(raw) // 2):]
    s = sorted(vals)
    med = statistics.median(s)
    return {
        "frames": len(s),
        "p50_us": pct(s, 0.50), "p95_us": pct(s, 0.95), "p99_us": pct(s, 0.99),
        "p999_us": pct(s, 0.999), "max_us": s[-1], "min_us": s[0],
        "mean_us": round(statistics.fmean(s), 1),
        "stddev_us": round(statistics.pstdev(s), 1),
        "jitter_cv": round(statistics.pstdev(s) / med, 4) if med else None,
        "p99_over_p50": round(pct(s, 0.99) / med, 3) if med else None,
        "stutters_2x_p50": sum(1 for v in s if v > 2 * med),
        "fps_from_p50": round(1e6 / med, 2) if med else None,
        "fps_1pct_low": round(1e6 / pct(s, 0.99), 2) if pct(s, 0.99) else None,
    }


def _kv_status(pid, keys):
    out = {}
    try:
        for line in pathlib.Path(f"/proc/{pid}/status").read_text().splitlines():
            k = line.split(":", 1)[0]
            if k in keys:
                out[k] = int(line.split()[1])          # kB
    except (FileNotFoundError, IndexError, ValueError):
        return {}
    return out

def _smaps_rollup(pid):
    out = {}
    try:
        for line in pathlib.Path(f"/proc/{pid}/smaps_rollup").read_text().splitlines():
            parts = line.split()
            if len(parts) >= 2 and parts[0].endswith(":"):
                try: out[parts[0].rstrip(":")] = int(parts[1])
                except ValueError: pass
    except (FileNotFoundError, IndexError, ValueError):
        return {}
    return out

def _cpu_ticks(pid):
    try:
        f = pathlib.Path(f"/proc/{pid}/stat").read_text().rsplit(")", 1)[1].split()
        return int(f[11]) + int(f[12])            # utime + stime
    except (FileNotFoundError, IndexError, ValueError):
        return None

def _faults(pid):
    try:
        f = pathlib.Path(f"/proc/{pid}/stat").read_text().rsplit(")", 1)[1].split()
        return int(f[7]), int(f[9])                    # minflt, majflt (dopo il campo 4)
    except (FileNotFoundError, IndexError, ValueError):
        return None

def sample_rss(pid, seconds, out):
    """Una riga al secondo: RSS, HWM, swap, anon/file/shmem, PSS, USS, minor/major faults."""
    end = time.time() + seconds
    keys = {"VmRSS", "VmHWM", "VmSwap", "RssAnon", "RssFile", "RssShmem"}
    hdr = ("t rss hwm swap anon file shmem pss uss minflt majflt cpu")
    hz = os.sysconf("SC_CLK_TCK") if hasattr(os, "sysconf") else 100
    prev_cpu = None
    with open(out, "w") as f:
        f.write(hdr + "\n")
        while time.time() < end:
            st = _kv_status(pid, keys)
            if not st:
                return
            sm = _smaps_rollup(pid)
            uss = sm.get("Private_Clean", 0) + sm.get("Private_Dirty", 0)
            fl = _faults(pid) or (0, 0)
            ct = _cpu_ticks(pid)
            cpu_pct = 0.0
            if ct is not None and prev_cpu is not None:
                cpu_pct = 100.0 * (ct - prev_cpu) / max(1, hz)
            prev_cpu = ct if ct is not None else prev_cpu
            f.write(f"{int(time.time())} {st.get('VmRSS',0)} {st.get('VmHWM',0)} "
                    f"{st.get('VmSwap',0)} {st.get('RssAnon',0)} {st.get('RssFile',0)} "
                    f"{st.get('RssShmem',0)} {sm.get('Pss',0)} {uss} {fl[0]} {fl[1]} {cpu_pct:.0f}\n")
            f.flush()
            time.sleep(1.0)


def run_scenario(scn, sock, seconds):
    """Drive the scenario. `gameplay` needs sc_ctl clicks with down/up + pause."""
    def ctl(*a):
        subprocess.run([sys.executable, str(SCCTL), "--sock", sock, *map(str, a)],
                       check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    def press(x, y, wait=1.2):
        ctl("move", x, y); time.sleep(0.3)
        ctl("down", x, y); time.sleep(0.25)
        ctl("up", x, y); time.sleep(wait)
    if scn != "gameplay":
        return
    press(220, 120, 2.0)      # Single Player
    press(262, 214, 2.5)      # StarCraft Original
    press(100, 109, 1.5)      # first profile
    press(517, 391, 3.0)      # Ok
    press(320, 140, 3.0)      # Terran
    press(320, 440, 2.0)      # Rebel Yell -> next
    press(320, 440, 2.5)      # Boot Camp -> briefing
    press(538, 390, 4.0)      # Start
    press(197, 262, 1.0)      # dismiss mission objectives


def one_run(args, run_idx):
    tagdir = PROF / args.tag / f"run-{run_idx}"
    tagdir.mkdir(parents=True, exist_ok=True)
    frames = tagdir / "frames.txt"
    alloc = tagdir / "alloc.txt"
    rss = tagdir / "rss.txt"
    env = dict(os.environ)
    env["SC_FRAME_LOG"] = str(frames)
    if args.alloc:
        env["LD_PRELOAD"] = str(ALLOC_SO)
        env["SC_ALLOC_LOG"] = str(alloc)
    for k, v in args.env:
        env[k] = v
    start = sh([str(ROOT / "tools" / "run-isolated.sh"), "start", "--id", f"prof-{args.tag}-{run_idx}",
                "--skip-intro", "0" if args.scenario == "intro" else "1", "--smack", "1"], env=env)
    if "alive=1" not in start.stdout:
        print(start.stdout, start.stderr)
        return None
    log = pathlib.Path(start.stdout.split("log=")[-1].strip())
    sock = log.parent / "sandbox" / "sc-ctl.sock"
    pid = int(start.stdout.split("game_pid=")[1].split()[0])
    time.sleep(args.settle)
    t0 = time.time()
    rss_proc = subprocess.Popen([sys.executable, __file__, "--_rss", str(pid), str(args.seconds), str(rss)]) \
        if False else subprocess.Popen([sys.executable, "-c",
            f"import sys;sys.path.insert(0,r'{pathlib.Path(__file__).parent}');"
            f"import importlib.util as u;s=u.spec_from_file_location('p','{__file__}');m=u.module_from_spec(s);"
            f"s.loader.exec_module(m);m.sample_rss({pid},{args.seconds},'{rss}')"])
    perf = None
    if args.perf:
        perf = subprocess.Popen(["perf", "stat", "-e",
            "cycles,instructions,cache-references,cache-misses,branches,branch-misses,page-faults",
            "-p", str(pid), "-o", str(tagdir / "perf.txt")],
            stderr=subprocess.DEVNULL)
    try:
        run_scenario(args.scenario, str(sock), args.seconds)
        while time.time() - t0 < args.seconds:
            time.sleep(0.5)
    finally:
        sh([str(ROOT / "tools" / "run-isolated.sh"), "stop", "--id", f"prof-{args.tag}-{run_idx}"])
        if perf:
            try: perf.terminate()
            except Exception: pass
            perf.wait(timeout=10)
        try: rss_proc.terminate()
        except Exception: pass
    st = stats_from_frames(frames, WARMUP_S)
    if not st:
        return None
    if rss.exists():
        lines = [l for l in rss.read_text().splitlines() if l.strip()]
        if len(lines) > 2:
            hdr = lines[0].split()
            rows = [dict(zip(hdr, r.split())) for r in lines[1:]]
            rows = [{k: int(v) for k, v in r.items()} for r in rows]
            mib = lambda k, agg="median": round(
                (statistics.median([r[k] for r in rows]) if agg == "median" else max(r[k] for r in rows)) / 1024, 1)
            st["rss_median_mib"] = mib("rss")
            st["rss_peak_mib"] = mib("hwm", "max")
            st["pss_median_mib"] = mib("pss")
            st["uss_median_mib"] = mib("uss")
            st["rss_anon_mib"] = mib("anon")
            st["rss_file_mib"] = mib("file")
            st["rss_shmem_mib"] = mib("shmem")
            st["swap_peak_mib"] = mib("swap", "max")
            secs = max(1, len(rows) - 1)
            st["minor_faults_per_s"] = round((rows[-1]["minflt"] - rows[0]["minflt"]) / secs, 1)
            st["major_faults_per_s"] = round((rows[-1]["majflt"] - rows[0]["majflt"]) / secs, 1)
            if "cpu" in rows[0]:
                st["cpu_pct_median"] = round(statistics.median([r["cpu"] for r in rows]), 1)
                st["cpu_pct_peak"] = max(r["cpu"] for r in rows)
    if alloc.exists():
        tot = {"malloc": 0, "free": 0, "mmap": 0, "munmap": 0, "bytes": 0}
        lines = [l for l in alloc.read_text().splitlines() if l.strip()]
        # count-alloc azzera i contatori a ogni riga: i valori stampati SONO già delta per secondo,
        # quindi si sommano (sottrarli come cumulativi dava totali negativi).
        for l in lines:
            d = dict(kv.split("=", 1) for kv in l.split()[1:])
            for k in ("malloc", "free", "mmap", "munmap", "bytes"):
                if k in d and d[k].isdigit():
                    tot[k] += int(d[k])
        secs = max(1, len(lines) - 1)
        st["alloc_per_s"] = {k: round(v / secs, 1) for k, v in tot.items()}
        st["alloc_per_frame"] = round(tot["malloc"] / max(1, st["frames"]), 2)
    if perf and (tagdir / "perf.txt").exists():
        txt = (tagdir / "perf.txt").read_text()
        if "instructions" in txt:
            st["perf"] = " ".join(txt.split("\n\n")[0].split()[:2])
    (tagdir / "summary.json").write_text(json.dumps(st, indent=2))
    fps_src = log.parent / "game.log"
    if fps_src.exists():
        fps = [l for l in fps_src.read_text(errors="ignore").splitlines() if l.startswith("[fps]")]
        (tagdir / "fps.txt").write_text("\n".join(fps[-args.seconds:]))
    print(f"  run {run_idx}: p50={st['p50_us']/1000:.2f}ms p95={st['p95_us']/1000:.2f} "
          f"p99={st['p99_us']/1000:.2f} max={st['max_us']/1000:.1f} fps50={st['fps_from_p50']} "
          f"jitterCV={st['jitter_cv']} rss={st.get('rss_median_mib','?')}MiB")
    return st


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--scenario", default="menu", choices=["menu", "intro", "gameplay"])
    ap.add_argument("--seconds", type=int, default=60)
    ap.add_argument("--repeat", type=int, default=3)
    ap.add_argument("--tag", required=True)
    ap.add_argument("--present", default="cpu")
    ap.add_argument("--settle", type=float, default=6.0)
    ap.add_argument("--perf", action="store_true")
    ap.add_argument("--alloc", action="store_true")
    ap.add_argument("--env", nargs=2, action="append", default=[])
    ap.add_argument("--_rss", nargs=3)
    args = ap.parse_args()
    if args._rss:
        sample_rss(int(args._rss[0]), float(args._rss[1]), args._rss[2])
        return
    args.env = list(args.env) + [("SC_PRESENT", args.present)]
    (PROF / args.tag).mkdir(parents=True, exist_ok=True)
    print(f"== {args.tag}: scenario={args.scenario} present={args.present} "
          f"{args.seconds}s x{args.repeat}")
    runs = []
    for i in range(1, args.repeat + 1):
        st = one_run(args, i)
        if st:
            runs.append(st)
    if not runs:
        print("nessuna esecuzione valida")
        return 1
    agg = {}
    for k in ("p50_us", "p95_us", "p99_us", "p999_us", "max_us", "jitter_cv", "p99_over_p50",
              "stutters_2x_p50", "fps_from_p50", "fps_1pct_low", "rss_median_mib", "rss_peak_mib",
              "pss_median_mib", "uss_median_mib", "rss_anon_mib", "rss_file_mib", "swap_peak_mib",
              "minor_faults_per_s", "major_faults_per_s", "alloc_per_frame",
              "cpu_pct_median", "cpu_pct_peak"):
        vals = [r[k] for r in runs if k in r and r[k] is not None]
        if vals:
            agg[k] = {"median": round(statistics.median(vals), 2),
                      "min": min(vals), "max": max(vals)}
    (PROF / args.tag / "aggregate.json").write_text(json.dumps({"runs": runs, "aggregate": agg}, indent=2))
    print(f"-- aggregato ({len(runs)} run) --")
    for k, v in agg.items():
        print(f"   {k:20s} med={v['median']:<12} min={v['min']:<12} max={v['max']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
