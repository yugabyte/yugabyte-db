#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh
source "${BASH_SOURCE[0]%/*}"/common_upgrade.sh

run_and_pushd_pg11

# Create pre-existing PG11 table
ysqlsh <<EOT
CREATE TABLE t (a int);
INSERT INTO t VALUES (1);
EOT

# Verify no PG15 catalog tables to begin with
bin/yb-admin --init_master_addrs=127.0.0.200:7100 list_tables include_table_id | grep -c 8001 \
  && exit 1

popd
upgrade_masters_run_ysql_catalog_upgrade
restart_node_2_in_pg15

# Verify there are PG15 catalog tables now
build/latest/bin/yb-admin --init_master_addrs=127.0.0.200:7100 list_tables include_table_id \
  | grep -c 8001

# Roll back
# Restart node 2 tserver as PG11
pushd $pg11path
# YB_TODO: Since D31087 isn't in the PG11 "from" build, we need to undo PG15's pg_data symlink
# and restore the pg_data_11 directory to pg_data.
# When D31087 is in the PG11 build, replace the lines from stop_node to start_node with:
#   yb_ctl restart_node 2
# D31087 is present in 2.21 onwards, 2024.1 onwards.
yb_ctl stop_node 2
rm "$data_dir/node-2/disk-1/pg_data"
mv "$data_dir/node-2/disk-1/pg_data_11" "$data_dir/node-2/disk-1/pg_data"
yb_ctl start_node 2 --tserver_flags="$pg11_enable_db_catalog_flag" --master_flags="$pg11_enable_db_catalog_flag"
popd
# Issue the rollback RPC
echo rollback starting at $(date +"%r")
build/latest/bin/yb-admin --init_master_addrs=127.0.0.200:7100 --timeout_ms=300000 \
  rollback_ysql_major_version_upgrade
echo rollback finished at $(date +"%r")

# Verify there are no PG15 catalog tables
build/latest/bin/yb-admin --init_master_addrs=127.0.0.200:7100 list_tables include_table_id \
  | grep -c 8001 && exit 1

# Start PG11 masters to complete the rollback. The whole cluster will be PG11.
pushd $pg11path
for i in {1..3}; do
  yb_ctl restart_node $i --master
done

# Verify there are no PG15 catalog tables
bin/yb-admin --init_master_addrs=127.0.0.200:7100 list_tables include_table_id \
  | grep -c 8001 && exit 1

# Make sure PG11 still works
echo making sure pg11 still works, starting at $(date +"%r")
diff <(ysqlsh <<EOT | sed 's/ *$//'
SHOW server_version_num;
INSERT INTO t VALUES (2);
SELECT * FROM t ORDER BY a;
EOT
) - <<EOT
 server_version_num
--------------------
 110002
(1 row)

INSERT 0 1
 a
---
 1
 2
(2 rows)

EOT

# Do the upgrade again
popd
upgrade_masters_run_ysql_catalog_upgrade

# Make sure PG11 still works (for PG15, only initdb has run)
echo making sure pg11 still works
ysqlsh <<EOT
SHOW server_version;
SELECT * FROM t;
EOT
echo pg11 still works

echo upgrade after rollback starting at $(date +"%r")
restart_node_2_in_pg15
echo upgrade after rollback finished at $(date +"%r")

verify_simple_table_mixed_cluster

# Restart and demo DDLs
yb_ctl restart
verify_simple_table_after_finalize
