#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh
source "${BASH_SOURCE[0]%/*}"/common_upgrade.sh

run_and_pushd_pg11
# Create pre-existing PG11 table
ysqlsh <<EOT
CREATE TABLE t (a int);
INSERT INTO t VALUES (1), (2);
EOT
popd

# Enter the phase where the masters are upgraded to PG15, but initdb hasn't run
upgrade_masters

# Start initdb twice and roll back twice, all simultaneously. Only one RPC of the 4 should succeed.
build/latest/bin/yb-admin --init_master_addrs=127.0.0.200:7100 --timeout_ms=300000 \
  ysql_major_version_upgrade_initdb &
pidi1=$!
build/latest/bin/yb-admin --init_master_addrs=127.0.0.200:7100 --timeout_ms=300000 \
  ysql_major_version_upgrade_initdb &
pidi2=$!
build/latest/bin/yb-admin --init_master_addrs=127.0.0.200:7100 --timeout_ms=300000 \
  rollback_ysql_major_version_upgrade &
pidr1=$!
build/latest/bin/yb-admin --init_master_addrs=127.0.0.200:7100 --timeout_ms=300000 \
  rollback_ysql_major_version_upgrade &
pidr2=$!

set +e
wait $pidi1
exiti1=$?
wait $pidi2
exiti2=$?
wait $pidr1
exitr1=$?
wait $pidr2
exitr2=$?
set -e

echo "exiti1=$exiti1, exiti2=$exiti2, exitr1=$exitr1, exitr2=$exitr2"
# Check that exactly one completed.
success_count=$(( ((exiti1 == 0)) + ((exiti2 == 0)) + ((exitr1 == 0)) + ((exitr2 == 0)) ))
test $success_count -eq 1

# Start initdb, and then roll back after a sleep. Only one RPC should succeed. Though this test is
# timing-dependent, it will succeed with either ordering and its intention is to test rollback
# blocked by initdb, which should happen the vast majority of the time.
build/latest/bin/yb-admin --init_master_addrs=127.0.0.200:7100 --timeout_ms=300000 \
  ysql_major_version_upgrade_initdb &
pidi1=$!
sleep 1
build/latest/bin/yb-admin --init_master_addrs=127.0.0.200:7100 --timeout_ms=300000 \
  rollback_ysql_major_version_upgrade &
pidr1=$!

set +e
wait $pidi1
exiti1=$?
wait $pidr1
exitr1=$?
set -e

echo "exiti1=$exiti1, exitr1=$exitr1"
# Check that exactly one completed
success_count=$(( ((exiti1 == 0)) + ((exitr1 == 0)) ))
echo "success count is $success_count"
test $success_count -eq 1

# Rollback needs to be slow for the next test to work, which requires initdb to have completed.
# If initdb hasn't run, run it.
if [ $exiti1 -ne 0 ]; then
  run_initdb
fi

# Trigger rollback, wait one second, and then start initdb. Though this test is timing-dependent, it
# will succeed with either ordering and its intention is to test initdb blocked by running rollback,
# which should happen the vast majority of the time.
build/latest/bin/yb-admin --init_master_addrs=127.0.0.200:7100 --timeout_ms=300000 \
  rollback_ysql_major_version_upgrade &
pidr1=$!
sleep 1
build/latest/bin/yb-admin --init_master_addrs=127.0.0.200:7100 --timeout_ms=300000 \
  ysql_major_version_upgrade_initdb &
pidi1=$!

set +e
wait $pidr1
exitr1=$?
wait $pidi1
exiti1=$?
set -e

echo "exiti1=$exiti1, exitr1=$exitr1"
# Check that exactly one completed
success_count=$(( ((exiti1 == 0)) + ((exitr1 == 0)) ))
echo "success count is $success_count"
test $success_count -eq 1

# The rest of the test script depends on initdb having completed.
# If hasn't run, run it.
if [ $exiti1 -ne 0 ]; then
  run_initdb
fi

# Verify upgrade still works
ysql_upgrade_using_node_2
verify_simple_table_mixed_cluster

# Restart and demo DDLs
yb_ctl restart
verify_simple_table_after_finalize
