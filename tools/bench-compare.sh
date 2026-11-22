#!/usr/bin/env bash
# Off-screen CPU/RAM comparison. Uses the existing native isolation runner and a
# dedicated Wine prefix; measurements are taken from /proc at 1 Hz.
set -Eeuo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
exec python3 - "$ROOT" "$@" <<'PY'
import csv, json, os, pathlib, re, signal, subprocess, sys, time
from statistics import median
ROOT=pathlib.Path(sys.argv[1]); OUT=ROOT/'build/qa/perf-compare'; OUT.mkdir(parents=True,exist_ok=True)
RUNS=int(os.environ.get('BENCH_RUNS','2')); DUR=int(os.environ.get('BENCH_SECONDS','5'))
ASSETS=pathlib.Path(os.environ.get('WINE_GAME_DIR','/home/enne2/dev/final-starcraft/Starcraft'))
PREFIX=pathlib.Path(os.environ.get('WINEPREFIX',ROOT/'build/qa/wine/prefix'))
RUNNER=ROOT/'tools/run-isolated.sh'; WINE=os.environ.get('WINE','wine')
DATA=[]; LOG=[]; launched=[]; WINE_PIDS=set()
(OUT/'error.txt').unlink(missing_ok=True)

def proc_stat(pid):
    try:
        s=pathlib.Path(f'/proc/{pid}/stat').read_text(); end=s.rfind(')'); f=s[end+2:].split()
        return int(f[1]), int(f[11])+int(f[12]), f[0] # ppid, utime+stime, state
    except (OSError,ValueError,IndexError): return None

def tree(root):
    allp={}
    for p in pathlib.Path('/proc').iterdir():
        if p.name.isdigit():
            st=proc_stat(int(p.name))
            if st: allp[int(p.name)]=st
    ids={root}; changed=True
    while changed:
        changed=False
        for p,(ppid,_,_) in allp.items():
            if p not in ids and ppid in ids: ids.add(p); changed=True
    return ids,allp

def snap(root):
    ids,allp=tree(root); ids.update(WINE_PIDS); ticks=os.sysconf('SC_CLK_TCK'); cpu=sum(allp[p][1] for p in ids if p in allp)/ticks
    rss_tree=0
    for p in ids:
        try:
            for line in pathlib.Path(f'/proc/{p}/status').read_text().splitlines():
                if line.startswith('VmRSS:'): rss_tree+=int(line.split()[1])*1024; break
        except OSError: pass
    roll={}
    try:
        for line in pathlib.Path(f'/proc/{root}/smaps_rollup').read_text().splitlines():
            m=re.match(r'(Rss|Pss|Private_Clean|Private_Dirty|Anonymous|Swap):\s+(\d+)',line)
            if m: roll[m.group(1)]=int(m.group(2))*1024
    except OSError: pass
    details={}
    for p in ids:
        try:
            comm=pathlib.Path(f'/proc/{p}/comm').read_text().strip()
            rss=next((int(line.split()[1])*1024 for line in pathlib.Path(f'/proc/{p}/status').read_text().splitlines() if line.startswith('VmRSS:')),0)
            details[str(p)]={'comm':comm,'rss_bytes':rss}
        except OSError: pass
    return {'cpu_tree_s':cpu,'tree_rss_bytes':rss_tree,'game':roll,'pids':sorted(ids),'processes':details}

def sample(target,run,phase,root,seconds=DUR):
    values=[]; previous=snap(root); t=time.monotonic()
    for n in range(seconds):
        deadline=t+n+1
        while time.monotonic()<deadline: time.sleep(min(.05,deadline-time.monotonic()))
        now=snap(root); dt=max(.01,time.monotonic()-(t+n)); delta=max(0,now['cpu_tree_s']-previous['cpu_tree_s'])
        values.append({'cpu_pct_core':delta/dt*100,'cpu_pct_norm':delta/dt/os.cpu_count()*100,
                       'tree_rss_bytes':now['tree_rss_bytes'],'pids':now['pids'],'processes':now['processes'],**now['game']})
        previous=now
    numeric=[k for k,v in values[0].items() if isinstance(v,(int,float))]
    agg={k:sum(x.get(k,0) for x in values)/len(values) for k in numeric}
    row={'target':target,'run':run,'phase':phase,'samples':values,'mean':agg,'pid':root}
    DATA.append(row); print(f"{target} run {run} {phase}: CPU {agg['cpu_pct_core']:.1f}% core ({agg['cpu_pct_norm']:.2f}% norm), RSS game/tree {agg.get('Rss',0)/2**20:.1f}/{agg['tree_rss_bytes']/2**20:.1f} MiB, PSS {agg.get('Pss',0)/2**20:.1f} MiB",flush=True)

def xtest(display,x,y,button=1):
    code='''import sys,time\nfrom Xlib import X,display\nd=display.Display(sys.argv[1]); r=d.screen().root\nfrom Xlib.ext import xtest\nx,y,b=map(int,sys.argv[2:]); xtest.fake_input(d,X.MotionNotify,x=x,y=y); d.sync(); time.sleep(.15); xtest.fake_input(d,X.ButtonPress,detail=b); xtest.fake_input(d,X.ButtonRelease,detail=b); d.sync()'''
    subprocess.run([sys.executable,'-c',code,display,str(x),str(y),str(button)],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)

def wine_tree_root(proc):
    # Follow the launched wine client PID; descendants include wineserver-side children where parented.
    return proc.pid

def run_native(i):
    tag=f'bench-n{i}-{int(time.time())}'; env=os.environ.copy(); env['SC_FPS_LOG']='1'
    p=subprocess.run([str(RUNNER),'start','--id',tag,'--skip-intro','1','--smack','1'],cwd=ROOT,env=env,text=True,capture_output=True)
    LOG.append(p.stdout+p.stderr); print(p.stdout.strip(),flush=True)
    m=re.search(r'display=(:\d+).*game_pid=(\d+)',p.stdout)
    if not m: raise RuntimeError('native runner did not return a game PID/display: '+p.stdout+p.stderr)
    disp,pid=m.groups(); pid=int(pid); launched.append(('native',tag,None))
    sample('native',i,'boot',pid)
    time.sleep(max(0,15-DUR)); sample('native',i,'menu',pid)
    # Navigate menu -> single player, campaign and Boot Camp. Allow transition intervals.
    for x,y,wait in [(220,120,2),(260,226,2),(100,109,1),(515,393,2),(305,250,2),(320,440,2),(320,440,2),(538,390,5),(197,262,3)]:
        try: subprocess.run([str(RUNNER),'cmd','--id',tag,f'click {x} {y}'],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        except Exception: pass
        time.sleep(wait)
    sample('native',i,'gameplay',pid)
    shot=OUT/f'native-{i}.png'; subprocess.run([str(RUNNER),'shot','--id',tag,'--out',str(shot)],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    (OUT/f'native-{i}.log').write_text((ROOT/'build/isolated'/tag/'game.log').read_text(errors='replace'))
    subprocess.run([str(RUNNER),'stop','--id',tag],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)

def run_wine(i):
    WINE_PIDS.clear()
    dnum=300+i; disp=f':{dnum}'; xvlog=OUT/f'wine-{i}-xvfb.log'
    xv=subprocess.Popen(['Xvfb',disp,'-screen','0','640x480x24','-nolisten','tcp','-noreset'],stdout=xvlog.open('w'),stderr=subprocess.STDOUT,start_new_session=True); launched.append(('wine',None,xv))
    for _ in range(100):
        if pathlib.Path(f'/tmp/.X11-unix/X{dnum}').exists(): break
        if xv.poll() is not None: raise RuntimeError(f'Xvfb {disp} failed')
        time.sleep(.05)
    env=os.environ.copy(); env.update({'DISPLAY':disp,'WINEPREFIX':str(PREFIX),'WINEDEBUG':'-all','SDL_AUDIODRIVER':'dummy','WINEDLLOVERRIDES':'winemenubuilder.exe=d'})
    env.pop('WAYLAND_DISPLAY',None); env.pop('XAUTHORITY',None); env.pop('XDG_RUNTIME_DIR',None)
    log=(OUT/f'wine-{i}.log').open('w')
    before={int(x.name) for x in pathlib.Path('/proc').iterdir() if x.name.isdigit()}
    p=subprocess.Popen([WINE,'StarCraft.exe'],cwd=ASSETS,env=env,stdout=log,stderr=subprocess.STDOUT,start_new_session=True); launched.append(('wineproc',p,log))
    time.sleep(2)
    # Wine detaches its Unix game process from the launcher. Discover that process
    # and the prefix's wineserver/explorer/services by comm, not by the launcher PID.
    gamepid=None
    for procdir in pathlib.Path('/proc').iterdir():
        if not procdir.name.isdigit(): continue
        try:
            pid=int(procdir.name); comm=(procdir/'comm').read_text().strip(); cmd=(procdir/'cmdline').read_bytes().replace(b'\\0',b' ').decode(errors='replace')
        except OSError: continue
        if comm in ('StarCraft.exe','wineserver','explorer.exe','services.exe','winedevice.exe'):
            # Select only the game client born from this launch, not an unrelated Wine session.
            if comm=='StarCraft.exe' and pid not in before: gamepid=pid
            if comm!='StarCraft.exe' or pid not in before: WINE_PIDS.add(pid)
    if gamepid is None: raise RuntimeError('Wine avviato ma processo StarCraft.exe non trovato; consultare wine log')
    sample('wine',i,'boot',gamepid)
    # Intro is longer than the native boot: allow it to finish, enter Single Player,
    # then capture an actual interactive menu window (rather than a movie frame).
    time.sleep(max(0,30-DUR))
    try: xtest(disp,220,120); time.sleep(2)
    except Exception as e: print(f'Wine XTEST menu click failed: {e}',flush=True)
    sample('wine',i,'menu',gamepid)
    try: subprocess.run(['import','-display',disp,'-window','root',str(OUT/f'wine-{i}-menu.png')],timeout=10,check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    except Exception as e: print(f'Wine menu screenshot unavailable: {e}',flush=True)
    for x,y,wait in [(260,226,2),(100,109,1),(515,393,2),(305,250,2),(320,440,2),(320,440,2),(538,390,5),(197,262,3)]:
        try: xtest(disp,x,y)
        except Exception as e: print(f'XTEST failed {disp} {x},{y}: {e}',flush=True); break
        time.sleep(wait)
    try: subprocess.run(['import','-display',disp,'-window','root',str(OUT/f'wine-{i}-after-input.png')],timeout=10,check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    except Exception as e: print(f'Wine post-input screenshot unavailable: {e}',flush=True)
    print(f'wine run {i}: gameplay non misurato; verificare screenshot post-input prima di considerarlo raggiunto',flush=True)
    try: os.killpg(p.pid,signal.SIGTERM); p.wait(timeout=5)
    except Exception: os.killpg(p.pid,signal.SIGKILL)
    # Stop Wine clients belonging to this dedicated prefix; wineserver shutdown uses this prefix only.
    subprocess.run([WINE,'wineboot','-k'],cwd=ASSETS,env=env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,timeout=20)
    try: os.killpg(xv.pid,signal.SIGTERM)
    except ProcessLookupError: pass

def cleanup():
    for kind,a,b in launched:
        if kind=='native': subprocess.run([str(RUNNER),'stop','--id',a],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        if kind=='wineproc' and a.poll() is None:
            try: os.killpg(a.pid,signal.SIGKILL)
            except ProcessLookupError: pass
        if kind=='wine' and b.poll() is None:
            try: os.killpg(b.pid,signal.SIGTERM)
            except ProcessLookupError: pass

try:
    if not (ROOT/'build/starcraft-x86').is_file(): raise RuntimeError('missing native executable')
    if not (ASSETS/'StarCraft.exe').is_file(): raise RuntimeError(f'missing {ASSETS}/StarCraft.exe')
    PREFIX.mkdir(parents=True,exist_ok=True)
    subprocess.run([WINE,'reg','add',r'HKCU\Software\Wine\Drivers','/v','Audio','/t','REG_SZ','/d','','/f'],env={**os.environ,'WINEPREFIX':str(PREFIX),'WINEDEBUG':'-all'},stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,timeout=30)
    for i in range(1,RUNS+1):
        run_native(i)
        run_wine(i)
except Exception as e:
    print(f'BENCH ERROR: {type(e).__name__}: {e}',file=sys.stderr,flush=True)
    (OUT/'error.txt').write_text(f'{type(e).__name__}: {e}\n')
finally: cleanup()
(OUT/'samples.json').write_text(json.dumps(DATA,indent=2))
with (OUT/'summary.csv').open('w',newline='') as f:
    w=csv.writer(f); w.writerow(['target','run','phase','cpu_pct_core','cpu_pct_norm','rss_mib','pss_mib','private_clean_mib','private_dirty_mib','anonymous_mib','swap_mib','tree_rss_mib'])
    for r in DATA:
        m=r['mean']; w.writerow([r['target'],r['run'],r['phase'],*[round(m.get(k,0)/2**20 if k in ('Rss','Pss','Private_Clean','Private_Dirty','Anonymous','Swap','tree_rss_bytes') else m.get(k,0),3) for k in ('cpu_pct_core','cpu_pct_norm','Rss','Pss','Private_Clean','Private_Dirty','Anonymous','Swap','tree_rss_bytes')]])
print(f'Artifacts: {OUT}',flush=True)
PY
