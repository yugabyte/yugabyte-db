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
upgrade_masters_run_initdb

# Run initdb again, make sure it works (idempotent)
run_initdb

# Verify upgrade after double initdb
ysql_upgrade_using_node_2

verify_simple_table_mixed_cluster

# Restart and demo DDLs
yb_ctl restart
verify_simple_table_after_finalize
