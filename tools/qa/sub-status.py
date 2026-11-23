#!/usr/bin/env python3
"""Poll the status of supervised pi subagents: id, busy, context tokens, last event."""
import json, sys, urllib.request, pathlib

for sid in sys.argv[1:]:
    p = pathlib.Path.home() / f".pi/agent/subagents/{sid}.json"
    try:
        rec = json.loads(p.read_text())
        req = urllib.request.Request(f"http://127.0.0.1:{rec['port']}/status",
                                     headers={"X-Pi-Token": rec["token"]})
        with urllib.request.urlopen(req, timeout=3) as r:
            s = json.loads(r.read())
        tok = s.get("context", {}).get("tokens", 0)
        print(f"{sid} {'BUSY' if s.get('busy') else 'IDLE'} tok={tok} label={rec.get('label')}")
    except Exception as e:
        print(f"{sid} ERR {type(e).__name__}: {e}")
