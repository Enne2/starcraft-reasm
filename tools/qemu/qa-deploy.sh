#!/usr/bin/env bash
# Compressed, atomic deployment over the loopback QEMU SSH forward; verify SHA-256 both sides.
# Usage: qa-deploy.sh LOCAL_FILE [REMOTE_PATH=/home/pi/sc/NAME] [TARGET=pi@127.0.0.1]
set -euo pipefail
[[ $# -ge 1 && $# -le 3 ]] || { echo "Usage: $0 LOCAL_FILE [REMOTE_PATH] [TARGET]" >&2; exit 2; }
local_file=$(realpath -- "$1")
[[ -f $local_file && -r $local_file ]] || { echo "file locale non leggibile: $local_file" >&2; exit 1; }
remote_path=${2:-/home/pi/sc/$(basename -- "$local_file")}
target=${3:-pi@127.0.0.1}
ssh_bin=${SSH_BIN:-"$(dirname -- "$(realpath -- "$0")")/../../build/qemu/ssh/gssh"}
[[ -x $ssh_bin ]] || { echo "client SSH non eseguibile: $ssh_bin (override: SSH_BIN=/path/ssh)" >&2; exit 1; }
# Constrain remote shell path syntax; avoids interpolating arbitrary shell metacharacters.
[[ $remote_path =~ ^/[A-Za-z0-9._/-]+$ && $remote_path != *"/../"* && $remote_path != */.. ]] || {
  echo "percorso remoto non sicuro/non supportato: $remote_path" >&2; exit 2;
}
[[ $remote_path != */ ]] || { echo "il path remoto deve nominare un file" >&2; exit 2; }
local_sha=$(sha256sum -- "$local_file" | awk '{print $1}')
local_mode=$(stat -c '%a' -- "$local_file")
tmp_path="${remote_path}.qa-tmp-$$"
remote_cmd="set -eu; tmp='$tmp_path'; dst='$remote_path'; trap 'rm -f \"\$tmp\"' EXIT; gzip -dc > \"\$tmp\"; chmod '$local_mode' \"\$tmp\"; mv -f -- \"\$tmp\" \"\$dst\"; trap - EXIT"
echo "deploy: $local_file -> $target:$remote_path"
echo "sha256 host: $local_sha"
gzip -c -- "$local_file" | "$ssh_bin" "$target" "$remote_cmd"
remote_out=$("$ssh_bin" "$target" "sha256sum '$remote_path'")
remote_sha=${remote_out%%[[:space:]]*}
echo "sha256 guest: $remote_sha"
[[ $remote_sha == "$local_sha" ]] || { echo "ERRORE: SHA-256 host/guest diversi" >&2; exit 1; }
echo "verifica SHA-256: OK"
