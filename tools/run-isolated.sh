#!/usr/bin/env bash
# Isolated, zero-sound, background multi-instance runner for starcraft-x86.
# Each --id gets its own Xvfb display (:100..:299), sandbox/characters dir,
# SC_CONTROL_FILE, screenshot capture, and dummy audio.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${SC_BIN:-$ROOT/build/starcraft-x86}"
SMACKER_SO="$ROOT/build/i386-libs/libsmacker-1.2.0.so"
ASSET_SRC="$ROOT/build/asset-sandbox"
ISO_BASE="$ROOT/build/isolated"

usage() {
  cat <<'EOF'
Usage:
  tools/run-isolated.sh start --id <id> [--skip-intro 1|0] [--smack 1|0] [--script NAME] [--replay FILE] [--trace 0|1]
  tools/run-isolated.sh cmd   --id <id> <command-line>
  tools/run-isolated.sh shot  --id <id> --out <path.png>
  tools/run-isolated.sh status --id <id>
  tools/run-isolated.sh stop  --id <id>
  tools/run-isolated.sh list
EOF
}

die() { echo "ERROR: $*" >&2; exit 1; }

alloc_display() {
  local d
  for d in $(seq 100 299); do
    if [ -e "/tmp/.X${d}-lock" ] || [ -e "/tmp/.X11-unix/X${d}" ]; then
      continue
    fi
    if mkdir "/tmp/sc-xvfb-${d}.lock" 2>/dev/null; then
      echo "$d"
      return 0
    fi
  done
  return 1
}

release_display() {
  local d="$1"
  [ -n "$d" ] && rmdir "/tmp/sc-xvfb-${d}.lock" 2>/dev/null || true
}

prepare_sandbox() {
  local sdir="$1"
  mkdir -p "$sdir/characters" "$sdir/Save"
  if [ -d "$ASSET_SRC/characters" ]; then
    cp -n "$ASSET_SRC/characters/"*.spc "$sdir/characters/" 2>/dev/null || true
  fi
  # StarCraft writes mission saves under Save/<character>/<name>.sng. The
  # original asset tree has neither Save/ nor the per-character directories;
  # prepare only directories for profiles actually present in this sandbox.
  local profile
  for profile in "$sdir/characters/"*.spc; do
    [ -f "$profile" ] || continue
    mkdir -p "$sdir/Save/$(basename "$profile" .spc)"
  done
  local item base
  for item in "$ASSET_SRC"/*; do
    base="$(basename "$item")"
    [ "$base" = "characters" ] && continue
    [ -e "$sdir/$base" ] || [ -L "$sdir/$base" ] || ln -s "$item" "$sdir/$base"
  done
}

cmd_start() {
  local id="" skip_intro="1" smack="" script="" replay="" trace="0"
  while [ $# -gt 0 ]; do
    case "$1" in
      --id) id="${2:?}"; shift 2 ;;
      --skip-intro) skip_intro="${2:?}"; shift 2 ;;
      --smack) smack="${2:?}"; shift 2 ;;
      --script) script="${2:?}"; shift 2 ;;
      --replay) replay="${2:?}"; shift 2 ;;
      --trace) trace="${2:?}"; shift 2 ;;
      *) die "unknown option: $1" ;;
    esac
  done
  [ -n "$id" ] || die "--id required"
  [ -x "$BIN" ] || die "missing $BIN"

  local stdir="$ISO_BASE/$id"
  if [ -f "$stdir/game.pid" ] && kill -0 "$(cat "$stdir/game.pid")" 2>/dev/null; then
    die "instance '$id' already running (pid $(cat "$stdir/game.pid"))"
  fi
  # clean stale lock if any
  if [ -f "$stdir/display" ]; then
    release_display "$(cat "$stdir/display" | tr -d ':')"
  fi

  mkdir -p "$stdir"
  local sdir="$stdir/sandbox"
  prepare_sandbox "$sdir"

  local disp
  disp="$(alloc_display)" || die "no free Xvfb display in 100..299"
  echo ":$disp" > "$stdir/display"

  : > "$stdir/cmd.txt"
  : > "$stdir/game.log"
  : > "$stdir/xvfb.log"

  env -u WAYLAND_DISPLAY -u XDG_RUNTIME_DIR -u XAUTHORITY \
    Xvfb ":$disp" -screen 0 640x480x24 -nolisten tcp -noreset \
    > "$stdir/xvfb.log" 2>&1 < /dev/null &
  local xpid=$!
  echo "$xpid" > "$stdir/xvfb.pid"

  local i=0
  while [ ! -S "/tmp/.X11-unix/X${disp}" ]; do
    kill -0 "$xpid" 2>/dev/null || { release_display "$disp"; die "Xvfb :$disp failed to start"; }
    sleep 0.05
    i=$((i + 1))
    [ "$i" -lt 60 ] || { kill -9 "$xpid" 2>/dev/null || true; release_display "$disp"; die "Xvfb :$disp timeout"; }
  done

  local -a env_args=(
    -u WAYLAND_DISPLAY -u XDG_RUNTIME_DIR -u XAUTHORITY
    "DISPLAY=:$disp"
    "SDL_VIDEODRIVER=x11"
    "SDL_AUDIODRIVER=dummy"
    "SC_FORCE_WINDOWED=1"
    "SC_SMACKER_SO=$SMACKER_SO"
    "SC_CONTROL_FILE=$stdir/cmd.txt"
    "SC_FPS_LOG=1"
  )
  [ "$skip_intro" = "1" ] && env_args+=("SC_SKIP_INTRO=1")
  [ -n "$smack" ] && env_args+=("SC_SMACK=$smack")
  [ -n "$script" ] && env_args+=("SC_INPUT_SCRIPT=$script")
  [ -n "$replay" ] && env_args+=("SC_INPUT_REPLAY=$replay")
  [ "$trace" = "1" ] && env_args+=("REASM_TRACE=1")
  # pass-through delle variabili di misurazione/profilo (inerenti al processo di gioco, non all'host)
  for v in SC_FRAME_LOG SC_ALLOC_LOG LD_PRELOAD SC_PROF SC_PRESENT SC_GL_VERIFY SC_FPS_CAP \
           SC_AFFINITY SC_NET_POLL_MS SC_PRESENT_DBG SC_SNP_TRACE SC_NO_GLOBAL_MOUSE; do
    eval "val=\${$v-}"; [ -n "$val" ] && env_args+=("$v=$val")
  done

  (
    cd "$sdir"
    setsid env "${env_args[@]}" "$BIN" > "$stdir/game.log" 2>&1 < /dev/null &
    echo $! > "$stdir/game.pid"
  )
  local gpid
  gpid="$(cat "$stdir/game.pid")"

  # Wait briefly for window creation or early exit
  local w=0
  while [ "$w" -lt 40 ]; do
    if grep -q '\[host\] finestra creata' "$stdir/game.log" 2>/dev/null; then
      break
    fi
    kill -0 "$gpid" 2>/dev/null || break
    sleep 0.1
    w=$((w + 1))
  done

  echo "id=$id display=:$disp xvfb_pid=$xpid game_pid=$gpid alive=$(kill -0 "$gpid" 2>/dev/null && echo 1 || echo 0) log=$stdir/game.log"
}

cmd_cmd() {
  local id=""
  while [ $# -gt 0 ]; do
    case "$1" in
      --id) id="${2:?}"; shift 2 ;;
      *) break ;;
    esac
  done
  [ -n "$id" ] || die "--id required"
  [ $# -ge 1 ] || die "command string required"
  local cfile="$ISO_BASE/$id/cmd.txt"
  [ -f "$cfile" ] || die "instance '$id' not initialized ($cfile missing)"
  printf '%s\n' "$@" >> "$cfile"
}

cmd_shot() {
  local id="" out=""
  while [ $# -gt 0 ]; do
    case "$1" in
      --id) id="${2:?}"; shift 2 ;;
      --out) out="${2:?}"; shift 2 ;;
      *) die "unknown option: $1" ;;
    esac
  done
  [ -n "$id" ] && [ -n "$out" ] || die "--id and --out required"
  local stdir="$ISO_BASE/$id"
  [ -f "$stdir/display" ] || die "no display for '$id'"
  local disp
  disp="$(cat "$stdir/display")"
  mkdir -p "$(dirname "$out")"
  env -u WAYLAND_DISPLAY -u XDG_RUNTIME_DIR -u XAUTHORITY DISPLAY="$disp" \
    import -window root "$out"
  echo "shot=$out"
}

cmd_status() {
  local id=""
  while [ $# -gt 0 ]; do
    case "$1" in
      --id) id="${2:?}"; shift 2 ;;
      *) die "unknown option: $1" ;;
    esac
  done
  [ -n "$id" ] || die "--id required"
  local stdir="$ISO_BASE/$id"
  [ -d "$stdir" ] || die "unknown instance '$id'"
  local disp gpid xpid galive=0 xalive=0
  disp="$(cat "$stdir/display" 2>/dev/null || echo '?')"
  gpid="$(cat "$stdir/game.pid" 2>/dev/null || echo '')"
  xpid="$(cat "$stdir/xvfb.pid" 2>/dev/null || echo '')"
  [ -n "$gpid" ] && kill -0 "$gpid" 2>/dev/null && galive=1
  [ -n "$xpid" ] && kill -0 "$xpid" 2>/dev/null && xalive=1
  echo "id=$id display=$disp game_pid=$gpid game_alive=$galive xvfb_pid=$xpid xvfb_alive=$xalive"
  tail -n 12 "$stdir/game.log" 2>/dev/null || true
}

cmd_stop() {
  local id=""
  while [ $# -gt 0 ]; do
    case "$1" in
      --id) id="${2:?}"; shift 2 ;;
      *) die "unknown option: $1" ;;
    esac
  done
  [ -n "$id" ] || die "--id required"
  local stdir="$ISO_BASE/$id"
  [ -d "$stdir" ] || return 0
  local gpid xpid disp
  gpid="$(cat "$stdir/game.pid" 2>/dev/null || true)"
  xpid="$(cat "$stdir/xvfb.pid" 2>/dev/null || true)"
  disp="$(cat "$stdir/display" 2>/dev/null | tr -d ':' || true)"
  [ -n "$gpid" ] && kill -9 "$gpid" 2>/dev/null || true
  [ -n "$xpid" ] && kill -9 "$xpid" 2>/dev/null || true
  [ -n "$disp" ] && release_display "$disp"
  echo "stopped id=$id"
}

cmd_list() {
  [ -d "$ISO_BASE" ] || return 0
  local d id disp gpid galive
  for d in "$ISO_BASE"/*; do
    [ -d "$d" ] || continue
    id="$(basename "$d")"
    disp="$(cat "$d/display" 2>/dev/null || echo '?')"
    gpid="$(cat "$d/game.pid" 2>/dev/null || echo '-')"
    galive=0
    [ "$gpid" != "-" ] && kill -0 "$gpid" 2>/dev/null && galive=1
    echo "id=$id display=$disp game_pid=$gpid alive=$galive"
  done
}

sub="${1:-}"
shift || true
case "$sub" in
  start) cmd_start "$@" ;;
  cmd) cmd_cmd "$@" ;;
  shot) cmd_shot "$@" ;;
  status) cmd_status "$@" ;;
  stop) cmd_stop "$@" ;;
  list) cmd_list "$@" ;;
  *) usage; exit 1 ;;
esac
