# Local reproduction: YSQL authentication catalog follower reads

Assumes a configured YugabyteDB build environment and a checkout of the full
stack. See the [design](ysql-catalog-follower-reads.md) for behavior and limits.

## Build

```bash
./yb_build.sh release daemons reinitdb --sj --skip-extra-pg-extensions --no-odyssey --no-ybc
```

Use `reinitdb`, not `initdb`, when reusing a build directory.

## Automated checks

Run one case per invocation. A skipped or zero-match run is not a pass.

```bash
for case in \
    PgAuthFollowerReadsTest.FreshPasswordLoginMembershipAndConnectPrivileges \
    PgAuthFollowerReadsTest.FollowerNetworkFailureFallsBackAtSameSnapshot \
    PgAuthFollowerReadsTest.SharedCatalogRestoreReachesFollowerLogins; do
  ./yb_build.sh release --cxx-test pg_auth_follower_reads-test --gtest_filter "$case" \
    --sj --skip-extra-pg-extensions --no-odyssey --no-ybc
done
```

[ysql-auth-follower-reads-tests.txt](ysql-auth-follower-reads-tests.txt) lists
the full regression set as `target case` lines. Logs are under
`build/latest/yb-test-logs/`.

## Manual cluster

Uses disposable data and loopback addresses `127.0.0.101` to `127.0.0.103`
(macOS needs loopback aliases for them).

```bash
set -euo pipefail

BUILD="$(cd build/latest && pwd -P)"
DATA="$(mktemp -d /tmp/ysql-auth.XXXXXX)/cluster"

ctl() {
  bin/yb-ctl --binary_dir "$BUILD" --data_dir "$DATA" "$@"
}

TS_FLAGS=ysql_enable_auth=true,enable_ysql_conn_mgr=false,ysql_enable_profile=false
TS_FLAGS+=,ysql_enable_read_request_cache_for_connection_auth=false
TS_FLAGS+=,ysql_enable_auth_catalog_follower_reads=true

ctl create --rf 3 --ip_start 101 \
  --master_flags ysql_enable_auth_catalog_follower_reads=true \
  --tserver_flags "$TS_FLAGS"

# Wait for YSQL to accept connections.
until PGPASSWORD=yugabyte "$BUILD/postgres/bin/ysqlsh" -X -w -h 127.0.0.101 -p 5433 \
    -U yugabyte -d yugabyte -Atc 'SELECT 1' >/dev/null 2>&1; do
  sleep 1
done

# Each invocation makes a fresh password-authenticated TCP connection.
for attempt in {1..5}; do
  PGPASSWORD=yugabyte "$BUILD/postgres/bin/ysqlsh" \
    -X -w -h 127.0.0.101 -p 5433 -U yugabyte -d yugabyte \
    -Atc 'SELECT current_user;'
done

# Follower-read admissions across all masters.
for suffix in 101 102 103; do
  echo "Master 127.0.0.$suffix"
  curl --noproxy '*' -fsS "http://127.0.0.$suffix:7000/prometheus-metrics" |
    grep '^ysql_auth_catalog_follower_reads'
done

ctl stop
echo "Data and logs retained at: $DATA"
```

Expect five successful logins and a positive follower-read counter on at least
one master. The counter counts read admissions, not successful logins. If it
stays at zero, check the flags on every process and that the authentication
response cache is disabled.
