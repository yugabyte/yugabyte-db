# Local reproduction: YSQL authentication catalog follower reads

The automated path below was validated on macOS arm64 in release mode with
Clang 21 and the Xcode macOS 15.2 SDK. It needs no separately managed cluster.
The manual Linux procedures are **UNEXECUTED**. Linux, debug, sanitizer,
mixed-binary, and throughput validation remain outstanding.

**Use only disposable local data. `--disable_pitr=true` is a new experimental
master flag in this stack. It permanently excludes PITR schedules, including
YCQL, and system-catalog restore. Do not point these commands at a real deployment.**

All masters must use compatible binaries before opt-in. Participating tserver/
PostgreSQL processes must be compatible before routing is enabled. The
[design](ysql-catalog-follower-reads.md) describes eligibility, permanent policy,
snapshot guarantees, and downgrade restrictions. Old experimental reservation
data remains unsupported; do not convert it.

## Get the full implementation

While this stack is under review, use its top PR, not an intermediate branch.
A fresh checkout keeps these binaries separate from any running local cluster:

```sh
git clone https://github.com/yugabyte/yugabyte-db.git ysql-auth-followers
cd ysql-auth-followers
git fetch origin pull/34482/head
git switch --detach FETCH_HEAD
```

Install the repository's build prerequisites for
[AlmaLinux](../../docs/content/stable/contribute/core-database/build-from-src-almalinux.md),
[Ubuntu](../../docs/content/stable/contribute/core-database/build-from-src-ubuntu.md), or
[macOS](../../docs/content/stable/contribute/core-database/build-from-src-macos.md).
Use Bash 4.4 or newer (on macOS, `brew install bash` and put Homebrew's `bin`
directory first on `PATH`; `/bin/bash` is too old). Open a child shell so a failed
command exits only that shell, leaving your terminal and scrollback intact:

```sh
bash --noprofile --norc
```

Run the remaining snippets in this child shell at the checkout root. The
[build guide](../../docs/content/stable/contribute/core-database/build-and-test.md)
describes compiler and build-type selection. Use only `yb_build.sh` to build and
run tests; do not invoke CMake, Ninja, or test binaries directly.

## Build

```sh
set -euo pipefail
: "${REPRO_ROOT:=$(mktemp -d "${TMPDIR:-/tmp}/ysql-auth-followers.XXXXXX")}"
DATA_DIR="$REPRO_ROOT/cluster"  # Used only by the optional manual procedure.
BUILD_TYPE=release
BUILD_ARGS=(--sj --skip-extra-pg-extensions --no-odyssey --no-ybc)
CMAKE_ARGS=()
printf 'Reproduction files: %s\n' "$REPRO_ROOT"
git rev-parse HEAD | tee "$REPRO_ROOT/revision.txt"
git status --short | tee "$REPRO_ROOT/working-tree.txt"

# The harness deletes TEST_TMPDIR at exit. Never point it at data you want to keep.
unset TEST_TMPDIR YB_GTEST_FILTER YB_EXTRA_GTEST_FLAGS
export YB_TEST_TMP_BASE_DIR="$REPRO_ROOT/test-data"
mkdir -p "$YB_TEST_TMP_BASE_DIR"
```

On macOS, add the settings used for validation before building. Xcode 16.2 ships
the macOS 15.2 SDK; adjust `DEVELOPER_DIR` if Xcode is installed elsewhere. Check
the selected SDK with `xcrun --sdk macosx --show-sdk-path`. Another SDK/toolchain
combination is unvalidated. Do not mix Command Line Tools and Xcode SDKs across
C++, PostgreSQL, and extensions. Linux users skip this block.

```sh
export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
export SDKROOT="$DEVELOPER_DIR/Platforms/MacOSX.platform/Developer/SDKs/MacOSX15.2.sdk"
test -d "$SDKROOT"
export PG_SYSROOT="$SDKROOT" YB_COMPILER_TYPE=clang21
CMAKE_ARGS=(--cmake-args "-DCMAKE_OSX_SYSROOT=$SDKROOT")
```

macOS mini-cluster tests also require loopback aliases. If not already configured,
add the test aliases below (requires local administrator access). Do not stop any
unrelated services using these addresses. Linux needs no aliases.

```sh
for suffix in 2 3 4 5 6 7; do
  if ! ifconfig lo0 | grep -Fq "inet 127.0.0.$suffix "; then
    sudo ifconfig lo0 alias "127.0.0.$suffix"
  fi
done
```

Now build daemons, PostgreSQL, `yb-admin`, and a matching initial catalog snapshot:

```sh
printf '%s\n' 'unset TEST_TMPDIR YB_GTEST_FILTER YB_EXTRA_GTEST_FLAGS' > "$REPRO_ROOT/env.sh"
declare -p REPRO_ROOT DATA_DIR BUILD_TYPE BUILD_ARGS CMAKE_ARGS YB_TEST_TMP_BASE_DIR \
  >> "$REPRO_ROOT/env.sh"
if [[ "$(uname -s)" == Darwin ]]; then
  declare -p DEVELOPER_DIR SDKROOT PG_SYSROOT YB_COMPILER_TYPE >> "$REPRO_ROOT/env.sh"
fi
./yb_build.sh "$BUILD_TYPE" daemons reinitdb --frcm \
  "${BUILD_ARGS[@]}" "${CMAKE_ARGS[@]}" 2>&1 | tee "$REPRO_ROOT/build.log"
BUILD_ROOT="$(cd build/latest && pwd -P)"
declare -p BUILD_ROOT >> "$REPRO_ROOT/env.sh"
test -x "$BUILD_ROOT/bin/yb-master"
test -x "$BUILD_ROOT/bin/yb-tserver"
test -x "$BUILD_ROOT/bin/yb-admin"
test -x "$BUILD_ROOT/postgres/bin/ysqlsh"
```

Keep `reinitdb` separate from test invocations. An older cached initial snapshot
lacks the template marker used by bootstrap recovery; `initdb` alone may reuse it.
Do not rebuild binaries while any cluster uses them. These commands do not use or
remove the default `~/yugabyte-data`, and they do not change default gflags.

Keep the printed `REPRO_ROOT` path. If the child shell exits, open another child
shell, source that directory's `env.sh`, then use `set -euo pipefail` again. Do not
start a new reproduction to inspect or stop an old cluster. The automated helper
below is also saved there once defined. Logs remain available after a failure.
If setup failed before `env.sh` was written, set `REPRO_ROOT` to the printed path
and repeat setup; no cluster has been started yet.
For a manual cluster, after restoring that environment you can stop it with
`bin/yb-ctl --binary_dir "$BUILD_ROOT" --data_dir "$DATA_DIR" stop`.

## Automated verification

Tests create, configure, and stop their own disposable clusters. No manual cluster
or `yb-ts-cli` is required. Run one exact fixture/case per invocation; a wildcard
suite, zero matched tests, or a skipped case is not a successful verification.

```sh
run_case() {
  local target="$1" case_name="$2" log
  shift 2
  log="$REPRO_ROOT/${target}-${case_name//\//_}.log"
  printf 'RUN %s (log: %s)\n' "$case_name" "$log"
  if ! ./yb_build.sh "$BUILD_TYPE" --cxx-test "$target" \
      --gtest_filter "$case_name" "${BUILD_ARGS[@]}" "$@" < /dev/null > "$log" 2>&1; then
    echo "FAIL: inspect $log" >&2
    tail -80 "$log"
    return 1
  fi
  if ! grep -Fq '[  PASSED  ] 1 test.' "$log" || grep -Fq '[  SKIPPED ]' "$log"; then
    echo "FAIL: require exactly one non-skipped test; inspect $log" >&2
    tail -80 "$log"
    return 1
  fi
  printf 'PASS %s (log: %s)\n' "$case_name" "$log"
}
declare -f run_case >> "$REPRO_ROOT/env.sh"

run_case pitr_disabled-test \
  PitrDisabledTest.RejectsYsqlAndYcqlPitr
run_case pitr_disabled-test \
  PitrEnabledTest.ExistingUniverseActivatesAcrossCoordinatedMasterRestart
run_case pitr_disabled-test \
  PitrEnabledTest.StartupBlocksAdmissionBeforeModeCommit
run_case pg_auth_follower_reads-test \
  PgAuthFollowerReadsTest.FreshPasswordLoginMembershipAndConnectPrivileges
run_case pg_auth_follower_reads-test \
  PgAuthFollowerReadsTest.FollowerNetworkFailureFallsBackAtSameSnapshot
```

These smoke cases check the permanent restriction, preservation of existing data
and configuration, admission blocked before commitment, fresh password and privilege
checks through actual follower admissions, and same-T leader fallback. Each command
must print `PASS`; use the full inventory for broader validation.

### Full regression inventory

[The test inventory](ysql-auth-follower-reads-tests.txt) contains the 92 cases used
for full-stack validation, including parameterized cases with their exact suffixes.
It covers 21 mode/recovery cases, authentication and cache exclusions, paging and
snapshot failures, client/master validation, clock and queue deadlines, historical
reads, catalog invalidation, and two existing CLI regressions. This is a selected
regression set, not the entire YugabyteDB test suite. Keep it in sync when renaming
or adding feature tests.

```sh
passed=0
while read -r target case_name || [[ -n "$target" ]]; do
  [[ -z "$target" || "$target" == \#* ]] && continue
  run_case "$target" "$case_name" < /dev/null || exit 1
  passed=$((passed + 1))
done < architecture/design/ysql-auth-follower-reads-tests.txt
test "$passed" -eq 92
printf 'PASS full inventory: %s cases\n' "$passed"
```

## Debug a failing case

The wrapper log includes build output, the exact test command, and daemon output.
Per-test `.log`, XML, and failure-detail files are under
`$BUILD_ROOT/yb-test-logs/`. Record the revision, platform, compiler/SDK, target,
exact case, and logs when reporting a failure; do not infer success from exit code
alone. Test logs and data can contain credentials, so use only synthetic fixtures
and inspect artifacts before sharing them.

To enable verbose test-process logging:

```sh
run_case pg_auth_follower_reads-test \
  PgAuthFollowerReadsTest.FollowerNetworkFailureFallsBackAtSameSnapshot \
  --test-args '--v=1'
```

Fixture teardown stops the cluster, and the harness removes temporary test data
even with `--test_leave_files=always`. Logs survive; data retention is not supported
by this recipe. External master/tserver processes have their own logging flags;
`--test-args` configures the test process, not every child. Use the manual cluster
below when you need long-lived servers or data directories to inspect.

To repeat one case serially while investigating intermittency:

```sh
./yb_build.sh "$BUILD_TYPE" --cxx-test pg_auth_follower_reads-test \
  --gtest_filter PgAuthFollowerReadsTest.FollowerNetworkFailureFallsBackAtSameSnapshot \
  "${BUILD_ARGS[@]}" -n 5 --tp 1 2>&1 | tee "$REPRO_ROOT/repeat.log"
test "$(grep -c 'PASSED: iteration' "$REPRO_ROOT/repeat.log")" -eq 5
```

Repeat mode prints the per-iteration log directory under
`~/logs/repeat_unit_test/` and updates `~/logs/latest_test`. It uses its own
`/tmp/yb_tests__*` directories rather than `YB_TEST_TMP_BASE_DIR`. Require exit zero
and five passing iterations; inspect the printed directory for failure details.

When the harness finds a core, it appends a backtrace to the test log (search for
`Found a core file at`) and deletes the core. For a core saved outside the harness,
such as from a manually run server, this optional **UNEXECUTED** command prints a
backtrace. Use the executable for the crashing process, not necessarily the test
executable:

```sh
# Replace both paths with the recorded core and its executable from BUILD_ROOT.
build-support/analyze_core_file.sh --core /path/to/core \
  --executable /path/to/matching/executable
```

For breakpoint-friendly builds, use a separate checkout, set `BUILD_TYPE=debug`,
and repeat the daemon/`reinitdb` build before running one case. Debug and sanitizer
runs are not part of the recorded validation. Release builds support the sync
points used by paging and fallback tests; do not bypass safe-time checks.

| Symptom | First check |
| --- | --- |
| Build fails around SDK headers such as `ptrdiff_t` | Use the same SDK for CMake, `SDKROOT`, and `PG_SYSROOT`; retry in a fresh isolated checkout if objects came from another SDK. |
| Bootstrap rejects initial-snapshot metadata | Rebuild `reinitdb` with this checkout. Do not convert reservation-prototype data. |
| Cannot bind a loopback address | Check macOS aliases and port conflicts; do not kill unrelated processes. |
| Startup refuses PITR exclusion | Read the identified blocker and recovery procedure below; do not clear the durable mode manually. |
| Auth snapshot RPC fails or follower metrics stay flat | Confirm persisted mode, routing flags on every master, postmaster restart, direct TCP, and disabled authentication response-cache selection. |
| `ServiceUnavailable` or `TimedOut` under login load | Inspect both snapshot-pool metrics below; increasing general RPC workers does not remove the dedicated limits. |

## Existing-universe activation (manual procedure UNEXECUTED)

Start with an existing disposable PITR-capable universe, not the reserved data
from an older prototype. No data-directory replacement or migration is required.

1. Install compatible binaries on all masters, leaving the request and routing
   flags false. Keep routing off until the durable mode is confirmed.
2. Inspect `yb-admin list_snapshot_schedules`, `list_snapshots SHOW_DELETED`, and
   `list_snapshot_restorations`. These summaries aid preflight; they do not replace
   the startup check of retained metadata and restoration finalization. Ordinary
   snapshots and finalized restore history do not block activation.
3. Resolve blockers explicitly through supported operations and allow cleanup to
   finish. Do not delete backup history merely to make a test pass.
4. Stop all masters before restarting any. Start them with `--disable_pitr=true`
   and routing still false. Do not use rolling `restart_node` operations for this
   transition. Expect temporary master/control-plane unavailability.
5. Require successful readiness and `pitrDisabled=true` from
   `yb-admin get_universe_config`, then follow the routing steps below.

A blocker fails startup without changing the policy. Recover by restarting
without the request, completing or cleaning up the work, and retrying the
coordinated restart. An interrupted write may already have committed: read the
persisted mode after recovery. Once true, flag removal cannot restore PITR.

Automated coverage includes
`PitrEnabledTest.ExistingUniverseActivatesAcrossCoordinatedMasterRestart`,
`PitrEnabledTest.StartupBlocksAdmissionBeforeModeCommit`, and the
`PitrExistingStartupTest`, `PitrExistingCrashTest`, and `PitrExistingWriteErrorTest`
cases. Select one exact case per `yb_build.sh` invocation.


## Optional manual Linux cluster (UNEXECUTED)

Use loopback addresses `127.0.0.101` through `127.0.0.103`, not a running cluster's
addresses. On Linux these are loopback addresses without aliases. macOS requires
separately configured loopback aliases; the commands here do not configure them.
Abort if any required port is already in use; do not stop unrelated processes.
The check below is a preflight, not protection against concurrent cluster starts.

Build the additional runtime-flag helper before starting this optional procedure:

```sh
./yb_build.sh "$BUILD_TYPE" --target yb-ts-cli "${BUILD_ARGS[@]}" \
  2>&1 | tee "$REPRO_ROOT/build-ts-cli.log"
test -x "$BUILD_ROOT/bin/yb-ts-cli"
```

```sh
: "${REPRO_ROOT:?Run the setup or source its env.sh}"
: "${BUILD_ROOT:?Run the build or source its env.sh}"
: "${DATA_DIR:?Run the setup or source its env.sh}"
test -d "$REPRO_ROOT"
[[ "$DATA_DIR" == "$REPRO_ROOT/cluster" && ! -e "$DATA_DIR" ]]

python3 - <<'PY'
import socket
for suffix in (101, 102, 103):
    for port in (7100, 9100, 7000, 9000, 5433, 9042, 6379, 11000, 12000, 13000):
        with socket.socket() as sock:
            sock.bind((f'127.0.0.{suffix}', port))
PY

export YB_DISABLE_CALLHOME=1
MASTERS=127.0.0.101:7100,127.0.0.102:7100,127.0.0.103:7100
MASTER_FLAGS_OFF=ysql_enable_auth_catalog_follower_reads=false
TS_FLAGS=ysql_enable_auth=true,enable_ysql_conn_mgr=false,ysql_enable_profile=false,ysql_enable_read_request_cache_for_connection_auth=false
ybctl() {
  bin/yb-ctl --binary_dir "${BUILD_ROOT:?}" --data_dir "${DATA_DIR:?}" "$@"
}
admin() {
  "$BUILD_ROOT/bin/yb-admin" --master_addresses "$MASTERS" "$@"
}

# --binary_dir takes the build root, not its bin subdirectory.
# This creation permanently disables PITR; DATA_DIR must be new.
ybctl create --rf 3 --ip_start 101 \
  --master_flags "disable_pitr=true,$MASTER_FLAGS_OFF" \
  --tserver_flags "$TS_FLAGS,ysql_enable_auth_catalog_follower_reads=false"
ybctl status
admin list_all_masters
admin get_universe_config
```

Require the persisted `pitr_disabled` mode to be true. `get_universe_config`
uses the existing `GetMasterClusterConfig` RPC; no reservation or promotion
step follows creation. Confirm three masters and three tservers use the same
compatible build. Servers reject PITR schedules (including YCQL) and
system-catalog restores, but ordinary backup snapshots and data-only restores
remain available.

For a bootstrap credential, this new local universe uses the standard
`yugabyte` role/password. All other credentials below are disposable fixtures.
Use explicit TCP and the direct PostgreSQL port, never a Unix socket or a
connection-manager endpoint:

```sh
YSQLSH="$BUILD_ROOT/postgres/bin/ysqlsh"
sql_admin() {
  PGPASSWORD=yugabyte "$YSQLSH" -X -w -h 127.0.0.101 -p 5433 \
    -U yugabyte -d yugabyte -v ON_ERROR_STOP=1 "$@"
}
sql_admin -c "CREATE ROLE auth_follower_demo LOGIN PASSWORD 'dummy_auth_pw_1';"
PGPASSWORD=dummy_auth_pw_1 "$YSQLSH" -X -w -h 127.0.0.101 -p 5433 \
  -U auth_follower_demo -d yugabyte -c 'SELECT current_user;'
```

Authentication response-cache selection wins over follower routing, including
cache misses. Do not infer routing from the feature flag alone. Keep
`ysql_enable_read_request_cache_for_connection_auth=false` explicitly even if a
build or environment enables that cache by default. Leave global
`ysql_enable_read_request_caching` unchanged. Profiles, connection-manager
backends, Unix sockets, and internal backends are outside this rollout.

## Enable routing only after confirming PITR-disabled mode

The flag must be enabled on all masters and the participating tservers and
PostgreSQL processes. `yb-admin` has no generic `set_flag` command here; the
supported runtime helper is `yb-ts-cli set_flag`, which also addresses a master's
generic server service. Enable masters first, then restart each tserver with the
new flag so its postmaster receives the forwarded gflag. A live tserver flag
change alone does not update an already-running postmaster's environment.

```sh
for suffix in 101 102 103; do
  "$BUILD_ROOT/bin/yb-ts-cli" --server_address="127.0.0.$suffix:7100" \
    set_flag ysql_enable_auth_catalog_follower_reads true
done
for node in 1 2 3; do
  ybctl restart_node "$node" \
    --tserver_flags "$TS_FLAGS,ysql_enable_auth_catalog_follower_reads=true"
done
```

Runtime master routing-flag changes do not survive restart. For a later full
restart, supply the routing flags again. `disable_pitr` is omitted deliberately:
the persisted mode cannot be removed by changing or omitting it, restart, or
leader failover.

```sh
ybctl restart \
  --master_flags 'ysql_enable_auth_catalog_follower_reads=true' \
  --tserver_flags "$TS_FLAGS,ysql_enable_auth_catalog_follower_reads=true"
admin get_universe_config  # PITR-disabled mode remains true.
```

## Observe admissions and exercise fresh authentication

Collect all masters, because leadership and replica selection can change:

```sh
auth_metrics() {
  for suffix in 101 102 103; do
    printf '\nmaster 127.0.0.%s\n' "$suffix"
    curl --noproxy '*' -fsS "http://127.0.0.$suffix:7000/prometheus-metrics" |
      grep -E '^ysql_auth_catalog_(snapshot_acquisitions|follower_reads|leader_reads)[{ ]'
  done
}
auth_metrics | tee "$REPRO_ROOT/metrics-before.txt"
for attempt in $(seq 1 30); do
  PGPASSWORD=dummy_auth_pw_1 "$YSQLSH" -X -w -h 127.0.0.101 -p 5433 \
    -U auth_follower_demo -d yugabyte -c 'SELECT current_user;' >/dev/null
done
auth_metrics | tee "$REPRO_ROOT/metrics-after.txt"

sql_admin -c "ALTER ROLE auth_follower_demo PASSWORD 'dummy_auth_pw_2';"
if PGPASSWORD=dummy_auth_pw_1 "$YSQLSH" -X -w -h 127.0.0.101 -p 5433 \
    -U auth_follower_demo -d yugabyte -c 'SELECT current_user;'; then
  echo 'ERROR: old password was accepted' >&2
  exit 1
fi
PGPASSWORD=dummy_auth_pw_2 "$YSQLSH" -X -w -h 127.0.0.101 -p 5433 \
  -U auth_follower_demo -d yugabyte -c 'SELECT current_user;'
sql_admin -c 'ALTER ROLE auth_follower_demo NOLOGIN;'
if PGPASSWORD=dummy_auth_pw_2 "$YSQLSH" -X -w -h 127.0.0.101 -p 5433 \
    -U auth_follower_demo -d yugabyte -c 'SELECT current_user;'; then
  echo 'ERROR: NOLOGIN role was accepted' >&2
  exit 1
fi
```

The two rejected connections must report authentication errors (wrong password
and a role not permitted to log in), not network or timeout failures.

Expect snapshot acquisitions on the leader and follower-read admissions on
nonleaders under healthy three-master operation. A positive follower delta shows
actual follower admission, not merely a preferred route. The read counters are
incremented after validation and safe-time checks but before storage execution;
they do not prove successful reads or logins. Failed passwords can increment them.
Leader admissions are possible on fallback or when the selected replica becomes
leader. This manual check does not prove same-T behavior under lag, timeouts, or
failover; use the automated read-path and integration tests for those cases.

If follower counters stay flat, check persisted mode, flags on every master/tserver,
postmaster restart, direct TCP, auth-response-cache selection, and profile
exclusion before interpreting it as a routing failure. Do not disable global
catalog caching or add test-only follower bypasses to force a metric increase.

### Capacity and latency checks

The leader establishes T at `MaxGlobalNow()`: expect up to roughly the clock-skew
bound of added login latency (500 ms with the default wall clock). Authentication
also pins database authorization and role settings at T; later phase-3 prefetch
cannot reuse shared-cache data at another snapshot. That bypass can increase
leader work for non-allowlisted catalogs. Follower admissions alone are not proof
of a net load reduction.

Snapshot waits use bounded dedicated pools, not ordinary RPC workers: 32 master
workers/128 outstanding tasks and 16 tserver workers/64 tasks, with 5000 ms
queue-plus-execution budgets. These are internal limits, not tuning flags. A full
pool returns `ServiceUnavailable`; expiration returns `TimedOut`. Neither permits
stale authentication.

Inspect `master_ysql_auth_snapshot_{task_limit_rejections,deadline_expirations,
outstanding_tasks}` on port 7000 and the corresponding `tserver_ysql_auth_snapshot_`
metrics on port 9000. Measure both layers, login latency, fallback frequency, and
leader catalog rows/bytes before considering real enablement. No throughput
improvement is established by this reproduction.

## Stop or disable routing without enabling PITR

To disable routing in this disposable cluster, restart with the original routing flags:

```sh
ybctl restart --master_flags "$MASTER_FLAGS_OFF" \
  --tserver_flags "$TS_FLAGS,ysql_enable_auth_catalog_follower_reads=false"
admin get_universe_config  # PITR-disabled mode remains true.
ybctl stop
printf 'Retained local data and logs: %s\n' "$REPRO_ROOT"
```

Stopping retains the unique data directory for inspection. No deletion command
is included. Do not use `wipe_restart`, `destroy`, or remove a default data
directory as part of this reproduction. Disabling routing is not a downgrade
procedure: PITR-disabled data must not be opened by mode-unaware binaries.
