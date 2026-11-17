#!/usr/bin/env bash
set -u
vr_here="$(cd -- "$(dirname -- "$0")" && pwd)"
vr_child=""
vr_command=""
vr_cleanup() {
  if [[ -n "$vr_child" ]]; then
    local vr_owned="$vr_child"
    vr_child=""
    kill -TERM "$vr_owned" 2>/dev/null || true
    wait "$vr_owned" 2>/dev/null || true
  fi
}
vr_stop() {
  local vr_job
  for vr_job in $(jobs -p); do
    if [[ "$vr_job" == "$vr_command" ]]; then kill -TERM "$vr_job" 2>/dev/null || true; fi
  done
  vr_cleanup
  exit 143
}
trap vr_cleanup EXIT
trap vr_stop INT TERM
if [[ -x "$vr_here/ed-vr-haptics" ]]; then
  "$vr_here/ed-vr-haptics" --auto --parent-pid "$$" &
  vr_child=$!
fi
if [[ $# -eq 0 ]]; then echo "Steam launch command is missing." >&2; exit 2; fi
"$@" &
vr_command=$!
wait "$vr_command"
vr_result=$?
vr_command=""
exit "$vr_result"
