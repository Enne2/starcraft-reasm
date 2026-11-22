#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
QEMU="${QEMU:-$ROOT/build/qemu-arm-build/qemu-system-arm}"
if [[ ! -x "$QEMU" ]]; then
  QEMU="$HOME/projects/zero95/.tools/qemu-build/qemu-system-arm"
fi
IMAGE="${IMAGE:-$ROOT/build/qemu/2026-04-13-raspios-bookworm-armhf.img}"
STATE="$ROOT/build/qemu/runtime"
QEMU_DISPLAY="${QEMU_DISPLAY:-sdl}"
# Kernel/DTB: default dall'immagine Bookworm estratta in build/qemu.
KERNEL="${KERNEL:-$ROOT/build/qemu/kernel.img}"
DTB="${DTB:-$ROOT/build/qemu/bcm2708-rpi-zero.dtb}"
# Disco di lavoro: overlay qcow2 sul IMAGE indicato.
DISK="${DISK:-$STATE/guest.qcow2}"
mkdir -p "$STATE"
[[ -x "$QEMU" && -r "$IMAGE" ]] || { echo "Missing QEMU or image: $QEMU / $IMAGE" >&2; exit 2; }
[[ -r "$KERNEL" ]] || { echo "Missing kernel: $KERNEL" >&2; exit 2; }
[[ -r "$DTB" ]] || { echo "Missing dtb: $DTB" >&2; exit 2; }
if [[ ! -f "$DISK" ]]; then
  qemu-img create -f qcow2 -F raw -b "$IMAGE" "$DISK"
  qemu-img resize "$DISK" 8G
fi
# Rete. NET_MODE=slirp (default) usa lo stack user-mode di QEMU;
# NET_MODE=passt avvia passt (userspace, nessun privilegio) e collega la NIC
# USB via socket UNIX: il guest riceve un IP della subnet reale, con NAT L4
# e porte inoltrate (PASST_TCP, default "2222:22").
NET_MODE="${NET_MODE:-slirp}"
NET_ARGS=()
if [[ "$NET_MODE" == "passt" ]]; then
  # Il socket va in /tmp: passt non riesce a crearlo dentro build/qemu/runtime.
  PASST_SOCK="${PASST_SOCK:-/tmp/starcraft-passt.sock}"
  rm -f "$PASST_SOCK"
  setsid nohup passt -f -4 --socket "$PASST_SOCK" \
    -t "${PASST_TCP:-2222:22}" \
    -a "${GUEST_IP:-10.0.0.241}/23" \
    -g "${GUEST_GW:-10.0.0.1}" \
    -D "${GUEST_DNS:-10.0.0.1}" \
    -m 1500 >"$STATE/passt.log" 2>&1 </dev/null &
  echo $! > "$STATE/passt.pid"
  sleep 1
  NET_ARGS=(-netdev "stream,id=net0,addr.type=unix,addr.path=$PASST_SOCK,server=off"
             -device "usb-net,netdev=net0")
elif [[ "${SSH_FWD:-0}" == "1" ]]; then
  NET_ARGS=(-netdev "user,id=net0,hostfwd=tcp:127.0.0.1:${SSH_PORT:-2222}-:22" -device "usb-net,netdev=net0")
elif [[ "${GUEST_NET:-0}" == "1" ]]; then
  NET_ARGS=(-netdev "user,id=net0" -device "usb-net,netdev=net0")
fi
: > "$STATE/serial.log"
DISPLAY_ARG="$QEMU_DISPLAY"
if [[ "$QEMU_DISPLAY" == "sdl" ]]; then
  export SDL_VIDEO_MINIMIZE_ON_FOCUS_LOSS="${SDL_VIDEO_MINIMIZE_ON_FOCUS_LOSS:-0}"
  DISPLAY_ARG="sdl"
fi
exec "$QEMU" -machine raspi0 -m 512M -name "Raspberry Pi Zero - StarCraft ARMv6" \
  -kernel "$KERNEL" \
  -dtb "$DTB" \
  -append "root=/dev/mmcblk0p2 rootfstype=ext4 rootwait rw console=tty1 console=ttyAMA0,115200 consoleblank=0 earlycon=pl011,0x20201000 loglevel=7 fsck.repair=yes initcall_blacklist=bcm2835_pm_driver_init dwc_otg.fiq_fsm_enable=0 plymouth.enable=0 ${KERNEL_APPEND:-}" \
  -drive "file=$DISK,format=qcow2,if=sd" \
  -chardev "socket,id=serial,path=$STATE/serial.sock,server=on,wait=off,logfile=$STATE/serial.log,logappend=off" \
  -serial chardev:serial -display "$DISPLAY_ARG" -no-reboot \
  -device usb-kbd -device usb-tablet \
  "${NET_ARGS[@]}" \
  -qmp "unix:$STATE/qmp.sock,server=on,wait=off" \
  "$@"
