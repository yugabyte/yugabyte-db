# Local reproduction: YSQL authentication catalog follower reads

Validated on macOS arm64 release: daemon/PostgreSQL/admin builds and 54 selected
tests passed, including all 22 PostgreSQL integration cases, with no skips.
The manual Linux cluster procedure below has not been executed. Use the automated
test first for a self-contained reproduction. No debug, sanitizer, mixed-binary,
or throughput validation is claimed.

**Use only a new, disposable local universe. Reservation permanently excludes
PITR, including YCQL schedules and system-catalog restore. There is no release
operation. Turning routing off, restarting, or demoting the capability does not
undo the reservation. Do not point these commands at a real deployment.**

The [design](ysql-catalog-follower-reads.md) describes the fixed-snapshot contract
and upgrade restrictions. Do not use mixed binaries or local overrides of
`ysql_enable_catalog_follower_read_reservation` as an activation procedure.

## Build and automated validation

Run from this checkout's root in Bash with the normal YugabyteDB build
prerequisites. Automated tests create their own disposable clusters; they do not
require the manual cluster or `yb-ts-cli`. On macOS, use the repository's normal
loopback-alias setup required by mini-cluster tests.

Use `yb_build.sh`, not CMake/Ninja or test binaries directly. These commands never
use or remove the default `~/yugabyte-data`.

```sh
set -euo pipefail
REPRO_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/ysql-auth-followers.XXXXXX")"
DATA_DIR="$REPRO_ROOT/cluster"  # Must not exist when yb-ctl create runs.
printf 'Reproduction files: %s\n' "$REPRO_ROOT"

./yb_build.sh release daemons initdb \
  --sj --skip-pg-parquet --no-odyssey --no-ybc \
  2>&1 | tee "$REPRO_ROOT/build.log"

# Needed only for the optional manual cluster procedure.
./yb_build.sh release --target yb-ts-cli \
  --sj --skip-pg-parquet --no-odyssey --no-ybc \
  2>&1 | tee "$REPRO_ROOT/build-ts-cli.log"

BUILD_ROOT="$(cd build/latest && pwd -P)"
test -x "$BUILD_ROOT/bin/yb-master"
test -x "$BUILD_ROOT/bin/yb-tserver"
test -x "$BUILD_ROOT/bin/yb-admin"
test -x "$BUILD_ROOT/bin/yb-ts-cli"
test -x "$BUILD_ROOT/postgres/bin/ysqlsh"
```

Prefer the automated integration target `pg_auth_follower_reads-test` for
fresh password/role changes, actual follower admissions, same-T fallback,
timeouts, cache/profile exclusions, and default-off behavior. Select one exact
fixture/case from `src/yb/yql/pgwrapper/pg_auth_follower_reads-test.cc` per
invocation, not a wildcard matching the whole suite. For example:

```sh
rg -n '^TEST(_F|_P)?\(' src/yb/yql/pgwrapper/pg_auth_follower_reads-test.cc
./yb_build.sh release --cxx-test pg_auth_follower_reads-test \
  --gtest_filter PgAuthFollowerReadsTest.FreshPasswordLoginMembershipAndConnectPrivileges \
  --sj --skip-pg-parquet --no-odyssey --no-ybc \
  2>&1 | tee "$REPRO_ROOT/integration-fresh-auth.log"

./yb_build.sh release --cxx-test yb-admin-test \
  --gtest_filter AdminCliTest.YsqlCatalogFollowerReadReservationRequiresAcknowledgement \
  --sj --skip-pg-parquet --no-odyssey --no-ybc \
  2>&1 | tee "$REPRO_ROOT/cli-acknowledgement.log"
./yb_build.sh release --cxx-test yb-admin-test \
  --gtest_filter AdminCliTest.YsqlCatalogFollowerReadReservation \
  --sj --skip-pg-parquet --no-odyssey --no-ybc \
  2>&1 | tee "$REPRO_ROOT/cli-reservation.log"
```

To run all 22 PostgreSQL cases, still one per invocation:

```sh
python3 - <<'PY' > "$REPRO_ROOT/pg-cases.txt"
import pathlib, re
for target in ('pg_auth_follower_reads-test', 'pg_auth_catalog_snapshot-test'):
    source = pathlib.Path(f'src/yb/yql/pgwrapper/{target}.cc').read_text()
    for fixture, case in re.findall(r'^TEST_F\((\w+), (\w+)\)', source, re.M):
        print(target, f'{fixture}.{case}')
PY
while read -r target case; do
  ./yb_build.sh release --cxx-test "$target" \
    --gtest_filter "$case" --sj --skip-pg-parquet --no-odyssey --no-ybc \
    > "$REPRO_ROOT/$case.log" 2>&1 || { tail -80 "$REPRO_ROOT/$case.log"; exit 1; }
  printf 'PASS %s\n' "$case"
done < "$REPRO_ROOT/pg-cases.txt"
```

Coverage includes concurrent password/HBA/CONNECT/settings changes, pagination,
real safe-time lag, in-flight failover, fixed-T fallback, restart, invalid request
admission, warm-cache rejection, and terminal snapshot failures. Tests also check
routing exclusions, normal-query cleanup, and tserver snapshot-pool saturation,
queue deadlines, and shutdown.

The initial daemon/initdb build is separate because `initdb` may not be built
when combined with test options. No default gflag values are changed by these
instructions.

Paging and same-T fallback tests use synchronization points available in release
builds. Run each case separately; a skipped case is not validation.

The other passing targets cover client routing (9 cases), master serving (4),
`ysql_auth_catalog_snapshot-test` (8), clock deadlines (3), reservation CLI (2),
and reservation, PITR, catalog-read-time, cache, and invalidation regressions (6).

## Optional manual Linux cluster (not yet executed)

Use loopback addresses `127.0.0.101` through `127.0.0.103`, not a running cluster's
addresses. On Linux these are loopback addresses without aliases. macOS requires
separately configured loopback aliases; the commands here do not configure them.
Abort if any required port is already in use; do not stop unrelated processes.
The check below is a preflight, not protection against concurrent cluster starts.

```sh
python3 - <<'PY'
import socket
for suffix in (101, 102, 103):
    for port in (7100, 9100, 7000, 9000, 5433, 9042, 6379, 11000, 12000, 13000):
        with socket.socket() as sock:
            sock.bind((f'127.0.0.{suffix}', port))
PY

export YB_DISABLE_CALLHOME=1
MASTERS=127.0.0.101:7100,127.0.0.102:7100,127.0.0.103:7100
MASTER_FLAGS_OFF=limit_auto_flag_promote_for_new_universe=0,ysql_enable_auth_catalog_follower_reads=false
TS_FLAGS=ysql_enable_auth=true,enable_ysql_conn_mgr=false,ysql_enable_profile=false,ysql_enable_read_request_cache_for_connection_auth=false
ybctl() {
  bin/yb-ctl --binary_dir "$BUILD_ROOT" --data_dir "$DATA_DIR" "$@"
}
admin() {
  "$BUILD_ROOT/bin/yb-admin" --master_addresses "$MASTERS" "$@"
}

# --binary_dir takes the build root, not its bin subdirectory.
ybctl create --rf 3 --ip_start 101 \
  --master_flags "$MASTER_FLAGS_OFF" \
  --tserver_flags "$TS_FLAGS,ysql_enable_auth_catalog_follower_reads=false"
ybctl status
admin list_all_masters
admin get_ysql_catalog_follower_read_reservation
```

Confirm three masters and three tservers use this same build. Expected initial
status is `reserved: false`, `reservation_pending: false`, and
`pitr_admitted_in_term: false`, plus a leader term. Reading status must not change
reservation state. The startup promotion limit keeps the capability unpromoted
until the explicit step below; it is not a local capability override.

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

## Promote, verify all peers, then irreversibly reserve

All peers must run compatible binaries before AutoFlag promotion. On this new
cluster, promote through `kLocalPersisted` (this also promotes other eligible
flags up to that class), then verify the capability is true on every master.
Do not substitute `set_flag` or a startup override for capability promotion.

```sh
admin promote_auto_flags kLocalPersisted
admin get_auto_flags_config
for suffix in 101 102 103; do
  curl --noproxy '*' -fsS "http://127.0.0.$suffix:7000/varz?raw=1" |
    grep -Fx -- '--ysql_enable_catalog_follower_read_reservation=true'
done
```

If a check fails, stop here and wait for propagation or investigate; recheck all
masters before proceeding. The advertised AutoFlags configuration alone is not
proof that every peer has applied it or runs compatible code.

**The next command is the irreversible opt-in. Proceed only for this disposable
universe and only if it will never need PITR.**

```sh
admin reserve_ysql_catalog_follower_reads acknowledge_permanent_pitr_exclusion
admin get_ysql_catalog_follower_read_reservation
# An acknowledged retry is idempotent.
admin reserve_ysql_catalog_follower_reads acknowledge_permanent_pitr_exclusion
```

Require `reserved: true` and `reservation_pending: false` before enabling
routing. `pitr_admitted_in_term` is transient admission information, not a promise
that PITR is available. If reservation times out, inspect status and retry; do not
assume the write was cancelled. Existing or retained PITR state blocks reservation.

## Enable routing only after reservation

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

Runtime master flag changes do not survive restart. For a later full restart of
this already-reserved local universe, supply the complete flags again:

```sh
ybctl restart \
  --master_flags 'limit_auto_flag_promote_for_new_universe=0,ysql_enable_auth_catalog_follower_reads=true' \
  --tserver_flags "$TS_FLAGS,ysql_enable_auth_catalog_follower_reads=true"
admin get_ysql_catalog_follower_read_reservation
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

If follower counters stay flat, check reservation, flags on every master/tserver,
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

## Stop or disable without releasing the reservation

To disable routing in this disposable cluster, restart with the original flags:

```sh
ybctl restart --master_flags "$MASTER_FLAGS_OFF" \
  --tserver_flags "$TS_FLAGS,ysql_enable_auth_catalog_follower_reads=false"
admin get_ysql_catalog_follower_read_reservation  # Still reserved: true.
ybctl stop
printf 'Retained local data and logs: %s\n' "$REPRO_ROOT"
```

Stopping retains the unique data directory for inspection. No deletion command
is included. Do not use `wipe_restart`, `destroy`, or remove a default data
directory as part of this reproduction. Disabling routing is not a downgrade
procedure: reserved data must not be opened by binaries that do not enforce
permanent PITR exclusion.
