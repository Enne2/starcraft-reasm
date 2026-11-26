#!/usr/bin/env python3
"""latency.py — input→visible-frame latency of the native port, measured over one socket.

The sc_ctl protocol is line based: we keep ONE connection open (a fresh `python tools/scctl`
process per sample would add ~50 ms of client startup and swamp the measurement, which is
exactly what happened in the first attempt).

For each sample:
  1. read the presented-frame counter (command `state`),
  2. send the key (`key <name>`) at time T0,
  3. poll `state` (sub-millisecond round trips) until the counter increments,
  4. latency = time from T0 to the reply that shows the first *new visible frame*.

This is display-level latency: it includes input queueing, the next game tick and the next present.
Usage: latency.py <socket> [samples]
"""
import socket, statistics, sys, time

def conn(path):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(path)
    s.settimeout(2.0)
    return s

def cmd(s, line):
    s.sendall(line.encode() + b"\n")
    buf = b""
    while not buf.endswith(b"\n"):
        chunk = s.recv(4096)
        if not chunk:
            raise RuntimeError("socket closed")
        buf += chunk
    return buf.decode(errors="replace").strip()

def presented(reply):
    for tok in reply.split():
        if tok.startswith("presented="):
            return int(tok.split("=", 1)[1])
    return -1

def main():
    sock = sys.argv[1]
    n = int(sys.argv[2]) if len(sys.argv) > 2 else 30
    s = conn(sock)
    cmd(s, "state")
    # warmup: a few samples discarded
    for _ in range(3):
        cmd(s, "keydown f1"); time.sleep(0.03); cmd(s, "keyup f1"); time.sleep(0.03)
    lat = []
    for i in range(n):
        p0 = presented(cmd(s, "state"))
        t0 = time.perf_counter()
        cmd(s, "keydown f1")
        while True:
            p1 = presented(cmd(s, "state"))
            if p1 != p0:
                lat.append((time.perf_counter() - t0) * 1000.0)
                cmd(s, "keyup f1")
                break
            if (time.perf_counter() - t0) > 0.5:
                cmd(s, "keyup f1")
                break
        time.sleep(0.1)
    s.close()
    lat.sort()
    if not lat:
        print("latency: nessun campione")
        return 1
    q = lambda f: lat[min(len(lat) - 1, int(f * (len(lat) - 1)))]
    print(f"latency n={len(lat)} p50={statistics.median(lat):.2f}ms "
          f"p95={q(0.95):.2f} p99={q(0.99):.2f} min={lat[0]:.2f} max={lat[-1]:.2f} "
          f"jitter_cv={statistics.pstdev(lat)/statistics.median(lat):.3f}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
