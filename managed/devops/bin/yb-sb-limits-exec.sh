#!/bin/bash
#
# Copyright 2026 YugabyteDB, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt

# Runs one support-bundle command on a DB node with best-effort CPU and memory limits, as the
# calling user and without sudo. YBA stages this file next to support_bundle_utils.sh for each
# bundle.
#
# Every bundle command on a node shares one budget. On cgroup v2 with a user systemd, the command
# runs in a transient scope under --slice, whose MemoryHigh, MemoryMax and CPUQuota cap all of them
# together. CPUQuota needs cpu delegated to user@<uid>; where it isn't, --cpu_fallback freeze puts
# the command in the slice's -throttled child, which one controller per node (this script with
# --controller, run as a user service) freezes (cgroup.freeze) for the rest of each 100 ms period.
# Elsewhere the command runs directly. Every path runs it under nice 19, ionice best-effort 7 and
# oom_score_adj 1000, then prints one WRAPPER_LIMITS line.
#
# The command follows --exec, not --: run_node_action.py's argparse ends --command at a bare --.

set -u

CGROUP_FS_ROOT="${CGROUP_FS_ROOT:-/sys/fs/cgroup}"
MEMINFO="${MEMINFO:-/proc/meminfo}"
PERIOD_US=100000
MIN_THAW_US=1000
CONTROLLER_UNIT=ybsupportbundle-throttle
CONTROLLER_MAX_SECS=21600
CONTROLLER_IDLE_SECS="${CONTROLLER_IDLE_SECS:-30}"
# A cap below a command's working set stalls it under memory.high instead of failing it, and a
# percent of MemAvailable gets that small on a busy node.
MEMORY_HIGH_FLOOR_BYTES=$((64 * 1024 * 1024))
MEMORY_MAX_FLOOR_BYTES=$((128 * 1024 * 1024))
SCRIPT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/$(basename "${BASH_SOURCE[0]}")"

cpu_percent=0
memory_high_percent=0
memory_max_percent=0
runtime_max_secs=0
unit=""
slice=ybsupportbundle.slice
cpu_fallback=none
no_cgroup=false
controller=false

usage() {
  [ -n "${1:-}" ] && echo "yb-sb-limits-exec.sh: $1" >&2
  echo "usage: yb-sb-limits-exec.sh [--cpu_percent N] [--memory_high_percent N]" \
    "[--memory_max_percent N] [--runtime_max_secs N] [--unit NAME] [--slice NAME]" \
    "[--cpu_fallback freeze|none] [--no_cgroup] --exec COMMAND [ARG...]" >&2
  echo "       yb-sb-limits-exec.sh --controller [--slice NAME] --cpu_percent N" >&2
  exit 2
}

int_arg() {
  [[ "${2:-}" =~ ^[0-9]+$ ]] || usage "$1 needs a non-negative integer"
}

while [ $# -gt 0 ]; do
  case "$1" in
    --cpu_percent) int_arg "$1" "${2:-}"; cpu_percent=$2; shift 2 ;;
    --memory_high_percent) int_arg "$1" "${2:-}"; memory_high_percent=$2; shift 2 ;;
    --memory_max_percent) int_arg "$1" "${2:-}"; memory_max_percent=$2; shift 2 ;;
    --runtime_max_secs) int_arg "$1" "${2:-}"; runtime_max_secs=$2; shift 2 ;;
    --unit)
      [[ "${2:-}" =~ ^[A-Za-z0-9_.:-]+$ ]] || usage "--unit needs a systemd unit name"
      unit=$2
      shift 2
      ;;
    --slice)
      [[ "${2:-}" =~ ^[A-Za-z0-9_][A-Za-z0-9_.:-]*\.slice$ ]] || usage "--slice needs a slice name"
      slice=$2
      shift 2
      ;;
    --cpu_fallback)
      [[ "${2:-}" =~ ^(freeze|none)$ ]] || usage "--cpu_fallback is freeze or none"
      cpu_fallback=$2
      shift 2
      ;;
    --no_cgroup) no_cgroup=true; shift ;;
    --controller) controller=true; shift ;;
    --exec) shift; break ;;
    *) usage "unknown option $1" ;;
  esac
done
if [ "$controller" = true ]; then
  [ $# -eq 0 ] || usage "--controller takes no command"
  [ "$cpu_percent" -gt 0 ] || usage "--controller needs --cpu_percent"
else
  [ $# -gt 0 ] || usage "--exec needs a command"
fi
# The range YBA documents and clamps to (percent of one core, percent of MemAvailable), applied
# again for any other caller; 0 still means no cap.
[ "$cpu_percent" -gt 100 ] && cpu_percent=100
[ "$memory_high_percent" -gt 100 ] && memory_high_percent=100
[ "$memory_max_percent" -gt 100 ] && memory_max_percent=100
if [ "$memory_max_percent" -gt 0 ] && [ "$memory_high_percent" -gt "$memory_max_percent" ]; then
  memory_high_percent=$memory_max_percent
fi
[ -n "$unit" ] || unit="yb-sb-$$-$RANDOM"
throttled_slice="${slice%.slice}-throttled.slice"

# systemctl --user finds the user manager and its bus through this. It is unset where yugabyte's
# .bashrc lacks YNP's export, e.g. in node agent's own environment when userEnv fails.
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"

# Computed rather than looked up, so it works before the slice is active. The base is the user
# manager's own cgroup (user@<uid>.service); systemd 252-46 reports no ControlGroup for init.scope,
# which is only the fallback. systemd nests a slice under each dash prefix of its name: a-b.slice is
# a.slice/a-b.slice.
slice_cgroup() {
  local manager part prefix="" path=""
  manager=$(systemctl --user show -P ControlGroup 2>/dev/null)
  if [ -z "$manager" ]; then
    manager=$(systemctl --user show -P ControlGroup init.scope 2>/dev/null)
    manager=${manager%/init.scope}
  fi
  [ -n "$manager" ] || return 1
  local IFS=-
  for part in ${1%.slice}; do
    prefix=${prefix:+$prefix-}$part
    path="$path/$prefix.slice"
  done
  echo "$CGROUP_FS_ROOT$manager$path"
}

# True while any process runs in the cgroup or below it.
populated() {
  local key value
  [ -f "$1/cgroup.events" ] || return 1
  while read -r key value; do
    [ "$key" = populated ] && { [ "$value" = 1 ]; return; }
  done <"$1/cgroup.events"
  return 1
}

# --- controller ---------------------------------------------------------------------------------

cg=""

# An external sleep: bash 5.1's read -t can lose its timeout to a signal and then block for good.
nap_us() {
  local t
  printf -v t '%d.%06d' $(($1 / 1000000)) $(($1 % 1000000))
  sleep "$t"
}

set_freeze() {
  echo "$1" 2>/dev/null >"$cg/cgroup.freeze"
}

read_usage() {
  local key value
  usage=-1
  read -r key value 2>/dev/null <"$cg/cpu.stat" && [ "$key" = usage_usec ] && usage=$value
}

# Each period the throttled slice is thawed for a window sized from the last period's usage, so the
# CPU all its commands use together stays near cpu_percent of one core, as CPUQuota would. A nap
# that wakes late (a saturated host) overshoots the window; that excess is kept as debt and paid
# back by whole frozen periods. A period is credited one budget however long it took, so lateness
# only makes the cap stricter. Exits once the slice has been empty for CONTROLLER_IDLE_SECS;
# ExecStopPost thaws it if this dies.
run_controller() {
  local budget=$((cpu_percent * PERIOD_US / 100)) thaw before used next debt=0
  local idle_since=$SECONDS
  cg=$(slice_cgroup "$throttled_slice") || exit 1
  [ "$budget" -lt "$MIN_THAW_US" ] && budget=$MIN_THAW_US
  thaw=$budget
  trap 'set_freeze 0; exit 0' TERM INT HUP
  while :; do
    if ! populated "$cg"; then
      set_freeze 0
      debt=0
      [ $((SECONDS - idle_since)) -ge "$CONTROLLER_IDLE_SECS" ] && break
      nap_us "$PERIOD_US"
      continue
    fi
    idle_since=$SECONDS
    read_usage
    before=$usage
    if [ "$before" -lt 0 ]; then
      nap_us "$PERIOD_US"
      continue
    fi
    if [ "$debt" -ge "$budget" ]; then
      set_freeze 1
      nap_us "$PERIOD_US"
      read_usage
      [ "$usage" -ge 0 ] && debt=$((debt + usage - before - budget))
      [ "$debt" -lt 0 ] && debt=0
      continue
    fi
    set_freeze 0
    nap_us "$thaw"
    if [ "$thaw" -lt "$PERIOD_US" ]; then
      set_freeze 1
      nap_us $((PERIOD_US - thaw))
      set_freeze 0
    fi
    read_usage
    [ "$usage" -ge 0 ] || continue
    used=$((usage - before))
    debt=$((debt + used - budget))
    [ "$debt" -lt 0 ] && debt=0
    if [ "$used" -gt 0 ]; then next=$((thaw * budget / used)); else next=$PERIOD_US; fi
    [ "$next" -gt $((thaw * 2)) ] && next=$((thaw * 2))
    [ "$next" -lt "$MIN_THAW_US" ] && next=$MIN_THAW_US
    [ "$next" -gt "$PERIOD_US" ] && next=$PERIOD_US
    thaw=$next
  done
  set_freeze 0
  exit 0
}

[ "$controller" = true ] && run_controller

# --- command ------------------------------------------------------------------------------------

applied_cpu_max=none
applied_memory_high=none
applied_memory_max=none
scope_slice=none
child=""

# ionice -t still runs the command if it can't set the class.
prefix=()
command -v nice >/dev/null 2>&1 && prefix+=(nice -n 19)
command -v ionice >/dev/null 2>&1 && prefix+=(ionice -t -c2 -n7)

# Only the command and what it starts take oom_score_adj 1000, so a node-wide OOM kills every
# bundle command before the wrapper, which then still reports.
exec_oom_first() {
  echo 1000 2>/dev/null >/proc/self/oom_score_adj
  exec "$@"
}

# The newline keeps the trailer on its own line when the command's output doesn't end with one.
trailer() {
  printf '\nWRAPPER_LIMITS cpu_mode=%s cpu_max=%s memory_high=%s memory_max=%s unit=%s' \
    "$1" "$applied_cpu_max" "$applied_memory_high" "$applied_memory_max" "$unit"
  printf ' slice=%s reason=%s\n' "$scope_slice" "$2"
}

# stdin is the wrapper's own script when YBA streams it, so the command never gets it. Outside a
# unit, timeout gives the command RuntimeMaxSec's bound; it has its own process group, so a TERM
# here is passed on, and the command runs in the background so the trap can fire.
run_direct() {
  local reason=$1
  shift
  unit=none
  scope_slice=none
  local bound=()
  if [ "$runtime_max_secs" -gt 0 ] && timeout -k 1 5 true >/dev/null 2>&1; then
    bound=(timeout -k 5 "$runtime_max_secs")
  fi
  (exec_oom_first ${bound[@]+"${bound[@]}"} "${prefix[@]}" "$@") </dev/null &
  local direct=$!
  # shellcheck disable=SC2064 # the pid is fixed now
  trap "kill -TERM $direct 2>/dev/null" TERM INT HUP
  local rc
  wait "$direct"
  rc=$?
  while kill -0 "$direct" 2>/dev/null; do
    wait "$direct"
    rc=$?
  done
  trailer none "$reason"
  exit "$rc"
}

# Starts the freeze-mode CPU controller (this script with --controller, as a user service) unless
# it runs already. There is one per node because every bundle's commands share the throttled slice:
# two controllers would undo each other's freezes and let the slice run past cpu_percent. systemd
# refuses a second unit of the same name, so a refusal while it is active or starting means it runs.
ensure_controller() {
  local throttled_cg state="" attempt err=""
  if ! throttled_cg=$(slice_cgroup "$throttled_slice"); then
    echo "yb-sb-limits-exec.sh: controller not started: no cgroup path for $throttled_slice" >&2
    return 1
  fi
  for ((attempt = 0; attempt < 5; attempt++)); do
    err=$(systemd-run --user --quiet --collect --unit="$CONTROLLER_UNIT" \
      -p RuntimeMaxSec="$CONTROLLER_MAX_SECS" -p TimeoutStopSec=5 \
      -p "ExecStopPost=-/bin/sh -c 'echo 0 > $throttled_cg/cgroup.freeze'" \
      -- bash "$SCRIPT" --controller --slice "$slice" --cpu_percent "$cpu_percent" \
      2>&1 >/dev/null </dev/null) && return 0
    state=$(systemctl --user is-active "$CONTROLLER_UNIT.service" 2>/dev/null)
    case "$state" in
      active | activating | reloading) return 0 ;;
    esac
    sleep 0.2
  done
  echo "yb-sb-limits-exec.sh: controller not started (${state:-unknown}): ${err:-no error text}" >&2
  return 1
}

[ "$no_cgroup" = true ] && run_direct no_cgroup "$@"
[ -f "$CGROUP_FS_ROOT/cgroup.controllers" ] || run_direct not_cgroup_v2 "$@"
systemctl --user show -p Version >/dev/null 2>&1 || run_direct no_user_manager "$@"

# Only the throttled child is ever frozen, so commands that must not be (--cpu_fallback none) run
# directly in the slice.
scope_slice=$slice
[ "$cpu_fallback" = freeze ] && scope_slice=$throttled_slice

props=(-p CPUWeight=1)
slice_limits=("CPUQuota=")
if [ "$cpu_percent" -gt 0 ]; then
  props+=(-p "CPUQuota=${cpu_percent}%")
  slice_limits=("CPUQuota=${cpu_percent}%")
fi
memory_high=infinity
memory_max=infinity
avail_kb=$(awk '/^MemAvailable:/ {print $2; exit}' "$MEMINFO" 2>/dev/null)
if [[ "$avail_kb" =~ ^[0-9]+$ ]]; then
  if [ "$memory_high_percent" -gt 0 ]; then
    memory_high=$((avail_kb * 1024 * memory_high_percent / 100))
    [ "$memory_high" -lt "$MEMORY_HIGH_FLOOR_BYTES" ] && memory_high=$MEMORY_HIGH_FLOOR_BYTES
  fi
  if [ "$memory_max_percent" -gt 0 ]; then
    memory_max=$((avail_kb * 1024 * memory_max_percent / 100))
    [ "$memory_max" -lt "$MEMORY_MAX_FLOOR_BYTES" ] && memory_max=$MEMORY_MAX_FLOOR_BYTES
  fi
fi
[ "$memory_high" != infinity ] && props+=(-p "MemoryHigh=$memory_high")
[ "$memory_max" != infinity ] && props+=(-p "MemoryMax=$memory_max")
slice_limits+=("MemoryHigh=$memory_high" "MemoryMax=$memory_max")
[ "$runtime_max_secs" -gt 0 ] && props+=(-p "RuntimeMaxSec=$runtime_max_secs" -p TimeoutStopSec=5)

# Rewritten only while no bundle command runs in the slice: the memory limits come from
# MemAvailable, and concurrent bundles would otherwise keep moving them under each other.
slice_cg=$(slice_cgroup "$slice")
if [ -z "$slice_cg" ] || ! populated "$slice_cg"; then
  systemctl --user set-property --runtime "$slice" "${slice_limits[@]}" >/dev/null 2>&1 ||
    echo "yb-sb-limits-exec.sh: could not set limits on $slice" >&2
fi

# A throwaway scope with the same properties shows what the kernel will apply, and lets a
# systemd-run failure fall back before the command has run, so it never runs twice.
# shellcheck disable=SC2016 # expanded by the probe's own shell, inside the scope
probe='d=/sys/fs/cgroup$(sed -n "s/^0:://p" /proc/self/cgroup)
for f in cpu.max memory.high memory.max; do
  v=$(cat "$d/$f" 2>/dev/null) || v=none
  echo "$f=$v"
done'
if ! preflight=$(systemd-run --user --scope --quiet --collect --slice="$scope_slice" \
  "${props[@]}" -- /bin/sh -c "$probe" 2>&1 </dev/null); then
  echo "yb-sb-limits-exec.sh: systemd-run failed: $preflight" >&2
  run_direct systemd_run_failed "$@"
fi
while IFS='=' read -r key value; do
  case "$key" in
    cpu.max) applied_cpu_max=${value// //} ;;
    memory.high) applied_memory_high=$value ;;
    memory.max) applied_memory_max=$value ;;
  esac
done <<<"$preflight"

mode=none
reason=none
if [ "$cpu_percent" -gt 0 ]; then
  if [[ "$applied_cpu_max" =~ ^[0-9] ]]; then
    mode=quota
  elif [ "$cpu_fallback" = freeze ]; then
    if ensure_controller; then mode=freeze; else reason=controller_failed; fi
  else
    reason=cpu_not_delegated
  fi
fi

# shellcheck disable=SC2317 # reached through the trap
on_signal() {
  systemctl --user stop "$unit.scope" >/dev/null 2>&1
  [ -n "$child" ] && kill "$child" 2>/dev/null
  exit 143
}
trap on_signal TERM INT HUP

exec_oom_first systemd-run --user --scope --quiet --collect --unit="$unit" --slice="$scope_slice" \
  "${props[@]}" -- "${prefix[@]}" "$@" </dev/null &
child=$!
wait "$child"
rc=$?
trailer "$mode" "$reason"
exit "$rc"
