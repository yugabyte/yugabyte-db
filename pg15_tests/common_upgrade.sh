# This file is sourced by each upgrade test. It contains shared code, such as
# functions and variables.

# Upgrade tests use a 3-node cluster and need to reference the second and third nodes.
pghost2=127.0.0.$((ip_start + 1))
pghost3=127.0.0.$((ip_start + 2))

# Downloads, runs, and pushds the directory for pg11.
# Sets $pg11path to the pg11 directory.
run_and_pushd_pg11() {
  prefix="/tmp"
  ybversion_pg11="2.20.2.2"
  ybbuild="b1"
  if [[ $OSTYPE = linux* ]]; then
    arch="linux-x86_64"
  fi
  if [[ $OSTYPE = darwin* ]]; then
    arch="darwin-x86_64"
  fi
  ybfilename_pg11="yugabyte-$ybversion_pg11-$ybbuild-$arch.tar.gz"

  if [ ! -d "$prefix"/"yugabyte-$ybversion_pg11" ]; then
    curl "https://downloads.yugabyte.com/releases/$ybversion_pg11/$ybfilename_pg11" \
      | tar xzv -C "$prefix"
  fi

  if [[ $OSTYPE = linux* ]]; then
    "$prefix/yugabyte-$ybversion_pg11/bin/post_install.sh"
  fi
  pg11path="$prefix/yugabyte-$ybversion_pg11"
  pushd "$pg11path"
  yb_ctl_destroy_create --rf=3 \
    --tserver_flags='"ysql_pg_conf_csv=yb_enable_expression_pushdown=false"'
}

upgrade_masters() {
  for i in {1..3}; do
    # Set master_join_existing_universe to true to mimic the configuration in YBA.
    # This flag is used to prohibit sys catalog creation. Setting it to true here tests that the
    # initdb RPC works with it on. With the flag set to false (the default), the system is more
    # permissive, so there shouldn't be a significant loss of test coverage by covering the YBA
    # case, and we avoid adding an entirely new test for this case.
    yb_ctl restart_node $i --master \
      --master_flags="TEST_online_pg11_to_pg15_upgrade=true,master_join_existing_universe=true"
  done
}

# On a Mac, initdb takes around 22 seconds on a release build, or over 90 seconds on debug. On
# Linux release it can take over 2 minutes.
run_initdb() {
  echo initdb starting at $(date +"%r")
  build/latest/bin/yb-admin --init_master_addrs=127.0.0.200:7100 --timeout_ms=300000 \
    ysql_major_version_upgrade_initdb
  echo initdb finished at $(date +"%r")
}

# Restarts the masters in a mode that's aware of the PG11 to PG15 upgrade process, then runs initdb.
# Must be run while the current directory is a pg15 directory, typically the directory for the
# source code being built.
upgrade_masters_run_initdb() {
  upgrade_masters
  run_initdb
}

# Upgrades the cluster to PG15, using node 2 as the PG15 tserver. Currently it upgrades only
# database "yugabyte", not global objects or other databases. On exit, node 2 is a working PG15
# tserver.
# Assumes node 2 is drained of all traffic before calling.
ysql_upgrade_using_node_2() {
  # Restart tserver 2 to PG15 for the upgrade, with postgres binaries in binary_upgrade mode
  yb_ctl restart_node 2 --tserver_flags='TEST_pg_binary_upgrade=true,"ysql_pg_conf_csv=yb_enable_expression_pushdown=false"'

  echo pg_upgrade starting at $(date +"%r")
  run_pg_upgrade
  echo pg_upgrade finished at $(date +"%r")

  # The upgrade is finished. Restart node 2 with postgres binaries *not* in binary upgrade mode
  yb_ctl restart_node 2 --tserver_flags='"ysql_pg_conf_csv=yb_enable_expression_pushdown=false"'
}

# Run pg_upgrade which calls ysql_dumpall, ysql_dump and pg_restore.
# ysql_dump and pg_restore together migrate the metadata for the databases.
# Currently, pg_upgrade migrates only the 'yugabyte' database.
run_pg_upgrade() {
  if [[ "$@" =~ "--check" ]]; then
    echo "$FUNCNAME: Use run_preflight_checks"
    exit 1
  fi
  build/latest/postgres/bin/pg_upgrade -D "$data_dir/node-2/disk-1/pg_data" \
    -h "$PGHOST" -p 5433 -H "$pghost2" -P 5433 --username "yugabyte"
}

run_preflight_checks() {
  build/latest/postgres/bin/pg_upgrade -d "$data_dir/node-1/disk-1/pg_data" \
    -h "$PGHOST" -p 5433 --username "yugabyte" --check
}

verify_simple_table_mixed_cluster() {
  # Insert from PG15
  diff <(ysqlsh 2 <<EOT | sed 's/ *$//'
SHOW server_version_num;
INSERT INTO t VALUES (15);
SELECT * FROM t ORDER BY a;
EOT
) - <<EOT
 server_version_num
--------------------
 150002
(1 row)

INSERT 0 1
 a
----
  1
  2
 15
(3 rows)

EOT

  # Insert from PG11, and note the PG15 insertion is visible
  diff <(ysqlsh <<EOT | sed 's/ *$//'
SHOW server_version_num;
INSERT INTO t VALUES (11);
SELECT * FROM t ORDER BY a;
EOT
) - <<EOT
 server_version_num
--------------------
 110002
(1 row)

INSERT 0 1
 a
----
  1
  2
 11
 15
(4 rows)

EOT
}

verify_simple_table_after_finalize() {
  diff <(ysqlsh <<EOT | sed 's/ *$//'
SHOW server_version_num;
SELECT * FROM t ORDER BY a;
CREATE INDEX ON t (a);
EXPLAIN (COSTS OFF) SELECT COUNT(*) FROM t WHERE a = 11;
SELECT COUNT(*) FROM t WHERE a = 11;
EOT
) - <<EOT
 server_version_num
--------------------
 150002
(1 row)

 a
----
  1
  2
 11
 15
(4 rows)

CREATE INDEX
                QUERY PLAN
------------------------------------------
 Finalize Aggregate
   ->  Index Only Scan using t_a_idx on t
         Index Cond: (a = 11)
         Partial Aggregate: true
(4 rows)

 count
-------
     1
(1 row)

EOT
}
