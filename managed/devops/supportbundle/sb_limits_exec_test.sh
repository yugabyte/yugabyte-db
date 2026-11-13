#!/bin/bash
#
# Copyright 2026 YugabyteDB, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt

# Tests bin/yb-sb-limits-exec.sh against stubbed systemd-run, systemctl, nice and ionice and a
# fake cgroup tree. The controller tests feed the throttled slice's cpu.stat from a simulator that
# only counts CPU while cgroup.freeze is 0. Both files must pass shellcheck, where it is installed.
# Run: bash devops/supportbundle/sb_limits_exec_test.sh

set -u

HERE=$(cd "$(dirname "$0")" && pwd)
WRAPPER="$(cd "$HERE/../bin" && pwd)/yb-sb-limits-exec.sh"
REAL_SLEEP=$(command -v sleep)
REAL_TIMEOUT=$(command -v timeout)
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
failures=0

MANAGER_CG="$TMP/cg/user.slice/user@1000.service"
SLICE_CG="$MANAGER_CG/ybsupportbundle.slice"
THROTTLED_CG="$SLICE_CG/ybsupportbundle-throttled.slice"

check() {
  if [ "$2" = "$3" ]; then
    echo "ok   $1"
  else
    echo "FAIL $1: got [$2], want [$3]"
    failures=$((failures + 1))
  fi
}

check_range() {
  if [ "$2" -ge "$3" ] && [ "$2" -le "$4" ]; then
    echo "ok   $1 ($2)"
  else
    echo "FAIL $1: got $2, want $3..$4"
    failures=$((failures + 1))
  fi
}

mkdir -p "$TMP/bin"
cat >"$TMP/bin/systemd-run" <<'EOF'
#!/bin/bash
echo "$*" >>"$STUB_LOG/systemd-run.log"
if [ "${STUB_RUN_FAIL:-}" = 1 ]; then
  echo "Failed to connect to bus" >&2
  exit 1
fi
unit=""
scope=false
while [ $# -gt 0 ] && [ "$1" != -- ]; do
  case "$1" in
    --unit=*) unit=${1#--unit=} ;;
    --scope) scope=true ;;
  esac
  shift
done
shift
if [ "$scope" = false ]; then
  # The controller service; refused while one is loaded, as systemd does.
  if [ "${STUB_CONTROLLER_FAIL:-}" = 1 ]; then
    echo "Failed to start transient service unit: Access denied" >&2
    exit 1
  fi
  if [ -e "$STUB_LOG/controller-active" ]; then
    echo "Failed to start transient service unit: Unit $unit.service was already loaded" >&2
    exit 1
  fi
  touch "$STUB_LOG/controller-active"
  exit 0
fi
if [ -z "$unit" ]; then
  echo "cpu.max=${STUB_CPU_MAX:-none}"
  echo "memory.high=${STUB_MEMORY_HIGH:-none}"
  echo "memory.max=${STUB_MEMORY_MAX:-none}"
  exit 0
fi
exec "$@"
EOF
cat >"$TMP/bin/systemctl" <<'EOF'
#!/bin/bash
echo "$*" >>"$STUB_LOG/systemctl.log"
echo "${XDG_RUNTIME_DIR:-unset}" >"$STUB_LOG/xdg"
case "$*" in
  "--user show -p Version") [ "${STUB_NO_MANAGER:-}" = 1 ] && exit 1; echo Version=252 ;;
  "--user show -P ControlGroup")
    [ "${STUB_NO_MANAGER_CG:-}" = 1 ] || echo /user.slice/user@1000.service
    ;;
  "--user show -P ControlGroup init.scope")
    [ "${STUB_INIT_SCOPE_CG:-}" = 1 ] && echo /user.slice/user@1000.service/init.scope
    ;;
  "--user is-active ybsupportbundle-throttle.service")
    if [ -e "$STUB_LOG/controller-active" ]; then echo active; else echo inactive; exit 3; fi
    ;;
  "--user set-property "*) [ "${STUB_SET_PROPERTY_FAIL:-}" = 1 ] && exit 1 ;;
esac
exit 0
EOF
cat >"$TMP/bin/nice" <<'EOF'
#!/bin/bash
shift 2
exec "$@"
EOF
cat >"$TMP/bin/ionice" <<'EOF'
#!/bin/bash
shift 3
exec "$@"
EOF
# Wakes STUB_SLEEP_EXTRA seconds late, the way a saturated host delays the controller's naps.
cat >"$TMP/bin/sleep" <<EOF
#!/bin/bash
$REAL_SLEEP "\$1"
[ -n "\${STUB_SLEEP_EXTRA:-}" ] && $REAL_SLEEP "\$STUB_SLEEP_EXTRA"
exit 0
EOF
# Counts CPU as permille-of-a-core while thawed, the way the kernel fills cpu.stat.
cat >"$TMP/simulate.sh" <<'EOF'
#!/bin/bash
dir=$1
permille=$2
usage=0
last=${EPOCHREALTIME/./}
while [ -e "$dir/cgroup.freeze" ]; do
  now=${EPOCHREALTIME/./}
  read -r frozen <"$dir/cgroup.freeze"
  [ "$frozen" = 0 ] && usage=$((usage + (now - last) * permille / 1000))
  last=$now
  echo "usage_usec $usage" >"$dir/cpu.stat.tmp" && mv "$dir/cpu.stat.tmp" "$dir/cpu.stat"
  sleep 0.002
done
EOF
chmod +x "$TMP/bin/"* "$TMP/simulate.sh"

# Fresh fake node per case: cgroup v2 root, a user manager, 4000000 kB available.
setup() {
  rm -rf "$TMP/cg" "$TMP/log"
  mkdir -p "$TMP/cg" "$TMP/log"
  echo "cpu memory pids" >"$TMP/cg/cgroup.controllers"
  printf 'MemTotal: 8000000 kB\nMemAvailable: 4000000 kB\n' >"$TMP/meminfo"
  echo "stdin data" >"$TMP/stdin"
  unset STUB_RUN_FAIL STUB_NO_MANAGER STUB_CPU_MAX STUB_MEMORY_HIGH STUB_MEMORY_MAX
  unset STUB_CONTROLLER_FAIL STUB_SET_PROPERTY_FAIL STUB_SLEEP_EXTRA STUB_NO_MANAGER_CG
  unset STUB_INIT_SCOPE_CG
}

run() {
  PATH="$TMP/bin:$PATH" CGROUP_FS_ROOT="$TMP/cg" MEMINFO="$TMP/meminfo" STUB_LOG="$TMP/log" \
    XDG_RUNTIME_DIR="$TMP" timeout 30 bash "$WRAPPER" "$@" <"$TMP/stdin" >"$TMP/out" 2>"$TMP/err"
  rc=$?
}

trailer() { grep '^WRAPPER_LIMITS ' "$TMP/out" | tail -1; }
field() { trailer | tr ' ' '\n' | sed -n "s/^$1=//p"; }
real_run() { grep -e '--scope' "$TMP/log/systemd-run.log" 2>/dev/null | grep -e '--unit='; }
controller_runs() {
  grep -e "--unit=ybsupportbundle-throttle" "$TMP/log/systemd-run.log" 2>/dev/null
}
set_property() { grep '^--user set-property' "$TMP/log/systemctl.log" 2>/dev/null; }

# --- usage ----------------------------------------------------------------------------------------
setup
run --cpu_percent 15
check "usage: no --exec exits 2" "$rc" 2
run --bogus --exec true
check "usage: unknown option exits 2" "$rc" 2
run --cpu_percent 1.5 --exec true
check "usage: non-integer exits 2" "$rc" 2
run --unit 'a b' --exec true
check "usage: bad unit name exits 2" "$rc" 2
run --slice notaslice --exec true
check "usage: bad slice name exits 2" "$rc" 2
run --controller --cpu_percent 15 --exec true
check "usage: controller with a command exits 2" "$rc" 2
run --controller
check "usage: controller without a budget exits 2" "$rc" 2

# --- direct paths -----------------------------------------------------------------------------
setup
run --no_cgroup --cpu_percent 15 --exec sh -c 'echo hi; exit 7'
check "no_cgroup: exit code passed through" "$rc" 7
check "no_cgroup: output kept" "$(head -1 "$TMP/out")" hi
check "no_cgroup: reason" "$(field reason)" no_cgroup
check "no_cgroup: cpu_mode" "$(field cpu_mode)" none
check "no_cgroup: no slice" "$(field slice)" none
check "no_cgroup: systemd-run not called" "$(cat "$TMP/log/systemd-run.log" 2>/dev/null)" ""

setup
rm "$TMP/cg/cgroup.controllers"
run --cpu_percent 15 --exec true
check "cgroup v1: reason" "$(field reason)" not_cgroup_v2

setup
export STUB_NO_MANAGER=1
run --cpu_percent 15 --exec true
check "no user manager: reason" "$(field reason)" no_user_manager

setup
export STUB_RUN_FAIL=1
run --cpu_percent 15 --exec sh -c "echo ran >>'$TMP/log/count'"
check "systemd-run fails: reason" "$(field reason)" systemd_run_failed
check "systemd-run fails: command ran once" "$(wc -l <"$TMP/log/count" | tr -d ' ')" 1
check "systemd-run fails: error on stderr" "$(grep -c 'Failed to connect' "$TMP/err")" 1
check "systemd-run fails: no slice" "$(field slice)" none

# A direct run gets RuntimeMaxSec's bound from timeout, and a TERM reaches the command's children.
setup
start=$SECONDS
run --no_cgroup --runtime_max_secs 1 --exec sleep 10
check "runtime bound: direct command stopped" "$rc" 124
check_range "runtime bound: within the bound" "$((SECONDS - start))" 0 4
check "runtime bound: trailer still printed" "$(field reason)" no_cgroup

setup
mkdir -p "$TMP/tbin"
printf '#!/bin/sh\nexit 125\n' >"$TMP/tbin/timeout"
chmod +x "$TMP/tbin/timeout"
PATH="$TMP/tbin:$TMP/bin:$PATH" CGROUP_FS_ROOT="$TMP/cg" MEMINFO="$TMP/meminfo" \
  STUB_LOG="$TMP/log" "$REAL_TIMEOUT" 30 bash "$WRAPPER" --no_cgroup --runtime_max_secs 5 \
  --exec sh -c 'exit 7' >"$TMP/out" 2>&1
check "runtime bound: skipped without a usable timeout" "$?" 7

setup
PATH="$TMP/bin:$PATH" CGROUP_FS_ROOT="$TMP/cg" MEMINFO="$TMP/meminfo" STUB_LOG="$TMP/log" \
  bash "$WRAPPER" --no_cgroup --runtime_max_secs 60 \
  --exec sh -c "sleep 30 & echo \$! >'$TMP/grandchild'; wait" >"$TMP/out" 2>&1 &
pid=$!
for _ in $(seq 1 25); do
  [ -s "$TMP/grandchild" ] && break
  "$REAL_SLEEP" 0.2
done
start=$SECONDS
kill -TERM "$pid"
wait "$pid"
check_range "runtime bound: TERM stops it promptly" "$((SECONDS - start))" 0 4
"$REAL_SLEEP" 1
grandchild=gone
kill -0 "$(cat "$TMP/grandchild")" 2>/dev/null && grandchild=alive
check "runtime bound: TERM reaches the command's children" "$grandchild" gone

# --- scope with CPUQuota ----------------------------------------------------------------------
setup
export STUB_CPU_MAX="15000 100000" STUB_MEMORY_HIGH=122880000 STUB_MEMORY_MAX=204800000
run --cpu_percent 15 --memory_high_percent 3 --memory_max_percent 5 --runtime_max_secs 60 \
  --unit yb-sb-test-1 --cpu_fallback freeze --exec sh -c 'echo out; exit 3'
check "quota: exit code passed through" "$rc" 3
check "quota: cpu_mode" "$(field cpu_mode)" quota
check "quota: cpu_max" "$(field cpu_max)" 15000/100000
check "quota: memory_high" "$(field memory_high)" 122880000
check "quota: unit" "$(field unit)" yb-sb-test-1
check "quota: slice" "$(field slice)" ybsupportbundle-throttled.slice
props="-p CPUWeight=1 -p CPUQuota=15% -p MemoryHigh=122880000 -p MemoryMax=204800000"
props="$props -p RuntimeMaxSec=60 -p TimeoutStopSec=5"
scope_opts="--user --scope --quiet --collect"
nice_cmd=" nice -n 19 ionice -t -c2 -n7 sh -c echo out; exit 3"
set_slice="--user set-property --runtime ybsupportbundle.slice"
check "quota: systemd-run argv" "$(real_run)" \
  "$scope_opts --unit=yb-sb-test-1 --slice=ybsupportbundle-throttled.slice $props --$nice_cmd"
check "quota: probe uses the same slice and properties" \
  "$(head -1 "$TMP/log/systemd-run.log" | sed 's/ -- .*//')" \
  "$scope_opts --slice=ybsupportbundle-throttled.slice $props"
check "quota: limits shared on the slice" "$(set_property)" \
  "$set_slice CPUQuota=15% MemoryHigh=122880000 MemoryMax=204800000"
check "quota: no controller" "$(controller_runs)" ""
check "quota: output then trailer" "$(sed -n '1p;3p' "$TMP/out" | cut -c1-14 | tr '\n' '|')" \
  "out|WRAPPER_LIMITS|"

setup
export STUB_CPU_MAX="15000 100000"
# shellcheck disable=SC2016 # the literal $HOME must reach the command unexpanded
run --cpu_percent 15 --exec printf '[%s]' 'a b' "q'uote" '*' '$HOME'
check "argv: arguments arrive intact" "$(head -1 "$TMP/out")" "[a b][q'uote][*][\$HOME]"
check "argv: trailer on its own line" "$(sed -n 2p "$TMP/out" | cut -c1-14)" WRAPPER_LIMITS

setup
export STUB_CPU_MAX="15000 100000"
run --cpu_percent 15 --exec cat
check "stdin: scoped command reads /dev/null" "$(head -1 "$TMP/out")" ""
run --no_cgroup --exec cat
check "stdin: direct command reads /dev/null" "$(head -1 "$TMP/out")" ""

setup
export STUB_CPU_MAX="15000 100000"
own_oom=$(cat /proc/self/oom_score_adj)
# The command's parent is the wrapper on both paths.
# shellcheck disable=SC2016 # expanded by the command's shell
oom_cmd=(sh -c 'cat /proc/self/oom_score_adj /proc/$PPID/oom_score_adj')
run --cpu_percent 15 --exec "${oom_cmd[@]}"
check "oom: command starts with oom_score_adj 1000" "$(sed -n 1p "$TMP/out")" 1000
check "oom: wrapper keeps its own" "$(sed -n 2p "$TMP/out")" "$own_oom"
run --no_cgroup --exec "${oom_cmd[@]}"
check "oom: direct command starts with 1000" "$(sed -n 1p "$TMP/out")" 1000
check "oom: direct wrapper keeps its own" "$(sed -n 2p "$TMP/out")" "$own_oom"

setup
export STUB_CPU_MAX="15000 100000"
env -u XDG_RUNTIME_DIR PATH="$TMP/bin:$PATH" CGROUP_FS_ROOT="$TMP/cg" MEMINFO="$TMP/meminfo" \
  STUB_LOG="$TMP/log" bash "$WRAPPER" --cpu_percent 15 --exec true >"$TMP/out" 2>&1
check "xdg: filled in when the caller has none" "$(cat "$TMP/log/xdg")" "/run/user/$(id -u)"
check "xdg: still scoped" "$(field cpu_mode)" quota
run --cpu_percent 15 --exec true
check "xdg: the caller's value is kept" "$(cat "$TMP/log/xdg")" "$TMP"

setup
run --exec true
check "no cpu limit: no CPUQuota on the scope" "$(real_run | grep -c CPUQuota)" 0
check "no cpu limit: cpu_mode" "$(field cpu_mode)" none
check "no limits: slice limits cleared" "$(set_property)" \
  "$set_slice CPUQuota= MemoryHigh=infinity MemoryMax=infinity"

# --- slice placement and limits -----------------------------------------------------------------
setup
export STUB_CPU_MAX="15000 100000"
run --cpu_percent 15 --cpu_fallback none --exec true
check "client: runs in the parent slice" "$(real_run | grep -o -e '--slice=[^ ]*')" \
  --slice=ybsupportbundle.slice

setup
export STUB_CPU_MAX="15000 100000"
run --cpu_percent 15 --slice sbtest.slice --cpu_fallback freeze --exec true
check "custom slice: throttled child" "$(field slice)" sbtest-throttled.slice
check "custom slice: limits on the parent" "$(set_property | cut -d' ' -f4)" sbtest.slice

setup
export STUB_CPU_MAX="15000 100000"
mkdir -p "$SLICE_CG"
printf 'populated 1\nfrozen 0\n' >"$SLICE_CG/cgroup.events"
run --cpu_percent 15 --memory_max_percent 5 --exec true
check "busy slice: limits left alone" "$(set_property)" ""

setup
export STUB_CPU_MAX="15000 100000"
mkdir -p "$SLICE_CG"
printf 'populated 0\nfrozen 0\n' >"$SLICE_CG/cgroup.events"
run --cpu_percent 15 --exec true
check "idle slice: limits set" "$(set_property | grep -c CPUQuota=15%)" 1

setup
export STUB_CPU_MAX="15000 100000" STUB_SET_PROPERTY_FAIL=1
run --cpu_percent 15 --exec sh -c 'echo ran'
check "set-property fails: command still runs" "$(head -1 "$TMP/out")" ran
check "set-property fails: reported" "$(grep -c 'could not set limits' "$TMP/err")" 1

# --- bounds -------------------------------------------------------------------------------------
memory_props() { grep -o 'Memory[A-Za-z]*=[0-9]*' | tr '\n' ' '; }

setup
export STUB_CPU_MAX="15000 100000"
run --cpu_percent 150 --exec true
check "bounds: cpu above 100% lowered on the scope" \
  "$(real_run | grep -o 'CPUQuota=[^ ]*')" CPUQuota=100%
check "bounds: and on the slice" "$(set_property | grep -o 'CPUQuota=[^ ]*')" CPUQuota=100%

setup
run --memory_high_percent 150 --memory_max_percent 200 --exec true
check "bounds: memory above 100% lowered" "$(real_run | memory_props)" \
  "MemoryHigh=4096000000 MemoryMax=4096000000 "

setup
run --memory_high_percent 20 --memory_max_percent 10 --exec true
check "bounds: soft above hard lowered to hard" "$(real_run | memory_props)" \
  "MemoryHigh=409600000 MemoryMax=409600000 "

setup
printf 'MemTotal: 2000000 kB\nMemAvailable: 400000 kB\n' >"$TMP/meminfo"
run --memory_high_percent 3 --memory_max_percent 5 --exec true
check "floors: busy node gets 64 MiB / 128 MiB on the scope" "$(real_run | memory_props)" \
  "MemoryHigh=67108864 MemoryMax=134217728 "
check "floors: and on the slice" "$(set_property | memory_props)" \
  "MemoryHigh=67108864 MemoryMax=134217728 "

setup
printf 'MemTotal: 2000000 kB\nMemAvailable: 400000 kB\n' >"$TMP/meminfo"
run --memory_max_percent 5 --exec true
check "floors: no soft limit asked, none set" "$(real_run | memory_props)" "MemoryMax=134217728 "

# --- cpu not delegated ------------------------------------------------------------------------
setup
run --cpu_percent 15 --cpu_fallback none --exec true
check "fallback none: cpu_mode" "$(field cpu_mode)" none
check "fallback none: reason" "$(field reason)" cpu_not_delegated
check "fallback none: no controller" "$(controller_runs)" ""

setup
run --cpu_percent 15 --cpu_fallback freeze --unit yb-sb-f1 --exec true
check "freeze: cpu_mode" "$(field cpu_mode)" freeze
check "freeze: reason" "$(field reason)" none
controller="--user --quiet --collect --unit=ybsupportbundle-throttle -p RuntimeMaxSec=21600"
controller="$controller -p TimeoutStopSec=5"
controller="$controller -p ExecStopPost=-/bin/sh -c 'echo 0 > $THROTTLED_CG/cgroup.freeze'"
controller="$controller -- bash $WRAPPER --controller --slice ybsupportbundle.slice"
controller="$controller --cpu_percent 15"
check "freeze: controller service" "$(controller_runs)" "$controller"
run --cpu_percent 15 --cpu_fallback freeze --unit yb-sb-f2 --exec true
check "freeze: a second command shares the running controller" "$(field cpu_mode)" freeze
check "freeze: one controller asked for twice, refused once" \
  "$(controller_runs | wc -l | tr -d ' ')/$(grep -c is-active "$TMP/log/systemctl.log")" 2/1

setup
export STUB_CONTROLLER_FAIL=1
run --cpu_percent 15 --cpu_fallback freeze --exec sh -c "echo ran >>'$TMP/log/count'"
check "no controller: reason" "$(field reason)" controller_failed
check "no controller: cpu_mode" "$(field cpu_mode)" none
check "no controller: five attempts" "$(controller_runs | wc -l | tr -d ' ')" 5
check "no controller: command ran once" "$(wc -l <"$TMP/log/count" | tr -d ' ')" 1
check "no controller: systemd-run's error reported" \
  "$(grep -c 'controller not started (inactive): Failed to start' "$TMP/err")" 1

# systemd 252-46 reports no ControlGroup for init.scope; older lookups may report only that one.
setup
export STUB_NO_MANAGER_CG=1 STUB_INIT_SCOPE_CG=1
run --cpu_percent 15 --cpu_fallback freeze --exec true
check "init.scope fallback: controller started" "$(field cpu_mode)" freeze
check "init.scope fallback: same throttled cgroup" \
  "$(controller_runs | grep -c -e "$THROTTLED_CG/cgroup.freeze")" 1

setup
export STUB_NO_MANAGER_CG=1
run --cpu_percent 15 --cpu_fallback freeze --exec true
check "no cgroup path: reason" "$(field reason)" controller_failed
check "no cgroup path: reported" "$(grep -c 'no cgroup path' "$TMP/err")" 1
check "no cgroup path: systemd-run not tried" "$(controller_runs)" ""

# --- controller ---------------------------------------------------------------------------------
start_controller() {
  PATH="$TMP/bin:$PATH" CGROUP_FS_ROOT="$TMP/cg" STUB_LOG="$TMP/log" XDG_RUNTIME_DIR="$TMP" \
    CONTROLLER_IDLE_SECS=1 bash "$WRAPPER" --controller --cpu_percent 15 >"$TMP/ctl.out" 2>&1 &
  ctl_pid=$!
}

# Prints "exited" once the controller is gone within $1 tenths of a second, else kills it.
controller_exits_within() {
  local i
  for ((i = 0; i < $1; i++)); do
    kill -0 "$ctl_pid" 2>/dev/null || { echo exited; return; }
    sleep 0.1
  done
  kill -9 "$ctl_pid" 2>/dev/null
  echo running
}

busy_slice() {
  mkdir -p "$THROTTLED_CG"
  echo 0 >"$THROTTLED_CG/cgroup.freeze"
  echo "usage_usec 0" >"$THROTTLED_CG/cpu.stat"
  printf 'populated 1\nfrozen 0\n' >"$THROTTLED_CG/cgroup.events"
}

# controller_share <sim permille> [late secs]: 3 s of a busy throttled slice, returns its use per
# mille, then empties the slice and checks the controller leaves.
controller_share() {
  setup
  [ -n "${2:-}" ] && export STUB_SLEEP_EXTRA=$2
  busy_slice
  "$TMP/simulate.sh" "$THROTTLED_CG" "$1" &
  local sim=$! start end usage
  start=${EPOCHREALTIME/./}
  start_controller
  sleep 3
  read -r _ usage <"$THROTTLED_CG/cpu.stat"
  end=${EPOCHREALTIME/./}
  printf 'populated 0\nfrozen 0\n' >"$THROTTLED_CG/cgroup.events"
  controller_exits_within 40 >"$TMP/ctl.exit"
  kill "$sim" 2>/dev/null
  echo $((usage * 1000 / (end - start)))
}

check_range "controller: 1 busy thread held near 15%" "$(controller_share 1000)" 110 200
check "controller: exits once the slice is idle" "$(cat "$TMP/ctl.exit")" exited
check "controller: thawed at exit" "$(cat "$THROTTLED_CG/cgroup.freeze")" 0
check_range "controller: 2 busy threads held near 15% in total" "$(controller_share 2000)" 110 200
check_range "controller: a 5% load is not throttled" "$(controller_share 50)" 35 60
check_range "controller: naps waking 20 ms late still held at or under 15%" \
  "$(controller_share 2000 0.02)" 50 200

setup
start_controller
check "controller: exits when the slice never appears" "$(controller_exits_within 40)" exited

setup
busy_slice
"$TMP/simulate.sh" "$THROTTLED_CG" 1000 &
sim=$!
start_controller
sleep 1
kill -TERM "$ctl_pid"
check "controller: stops on TERM" "$(controller_exits_within 20)" exited
check "controller: thawed on TERM" "$(cat "$THROTTLED_CG/cgroup.freeze")" 0
kill "$sim" 2>/dev/null

# --- abort ------------------------------------------------------------------------------------
setup
export STUB_CPU_MAX="15000 100000"
PATH="$TMP/bin:$PATH" CGROUP_FS_ROOT="$TMP/cg" MEMINFO="$TMP/meminfo" STUB_LOG="$TMP/log" \
  XDG_RUNTIME_DIR="$TMP" bash "$WRAPPER" --cpu_percent 15 --unit yb-sb-abort --exec sleep 30 \
  >"$TMP/out" 2>&1 &
pid=$!
sleep 1
kill -TERM "$pid"
wait "$pid"
check "abort: exit 143" "$?" 143
check "abort: scope stopped" "$(grep -c 'stop yb-sb-abort.scope' "$TMP/log/systemctl.log")" 1
check "abort: controller left alone" "$(grep -c 'stop ybsupportbundle' "$TMP/log/systemctl.log")" 0

# --- shellcheck ---------------------------------------------------------------------------------
if command -v shellcheck >/dev/null 2>&1; then
  check "shellcheck: wrapper and this test are clean" \
    "$(shellcheck -x "$WRAPPER" "$HERE/$(basename "$0")" 2>&1)" ""
else
  echo "skip: shellcheck is not installed on this host"
fi

if [ "$failures" -gt 0 ]; then
  echo "$failures check(s) failed"
  exit 1
fi
echo "All yb-sb-limits-exec.sh checks passed"
