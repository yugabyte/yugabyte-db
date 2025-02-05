# This file is sourced by each upgrade test. It contains shared code, such as
# functions and variables.

# Upgrade tests use a 3-node cluster and need to reference the second and third nodes.
pghost2=127.0.0.$((ip_start + 1))
pghost3=127.0.0.$((ip_start + 2))

# TEST_always_return_consensus_info_for_succeeded_rpc=false is needed to upgrade a release build to
# debug.
common_pg15_flags="TEST_always_return_consensus_info_for_succeeded_rpc=false"
# yb_enable_expression_pushdown=false is needed because the expression pushdown rewriter is not yet
# implemented.
common_tserver_flags='"ysql_pg_conf_csv=yb_enable_expression_pushdown=false"'

# Downloads, runs, and pushds the directory for pg11.
# Sets $pg11path to the pg11 directory.
run_and_pushd_pg11() {
  prefix="/tmp"
  ybversion_pg11="2024.2.1.0"
  ybbuild="b116"
  ybhash="ef5232e8f428fba9d52c6cc2002d46ffd79ab999"
  if [[ $OSTYPE = linux* ]]; then
    arch="release-clang17-centos-x86_64"
    tarbin="tar"
  fi
  if [[ $OSTYPE = darwin* ]]; then
    arch="release-clang-darwin-arm64"
    tarbin="gtar"
  fi
  ybfilename_pg11="yugabyte-$ybversion_pg11-$ybbuild-$arch.tar.gz"
  ybfilename_pg11_web="yugabyte-$ybversion_pg11-$ybhash-$arch.tar.gz"

  if [ ! -f "$prefix"/"$ybfilename_pg11" ]; then
    curl "https://s3.us-west-2.amazonaws.com/uploads.dev.yugabyte.com/local-provider-test/$ybversion_pg11-$ybbuild/$ybfilename_pg11_web" \
      -o "$prefix"/"$ybfilename_pg11"
  fi

  "$tarbin" xzf "$prefix"/"$ybfilename_pg11" --skip-old-files -C "$prefix"

  if [[ $OSTYPE = linux* ]]; then
    "$prefix/yugabyte-$ybversion_pg11/bin/post_install.sh"
  fi

  pg11path="$prefix/yugabyte-$ybversion_pg11"
  pushd "$pg11path"
  yb_ctl_destroy_create --rf=3 --tserver_flags="$common_tserver_flags"
}

upgrade_masters() {
  for i in {1..3}; do
    yb_ctl restart_node $i --master \
      --master_flags="master_join_existing_universe=true,$common_pg15_flags"
  done
}

run_ysql_catalog_upgrade() {
  echo run_ysql_catalog_upgrade starting at $(date +"%r")
  build/latest/bin/yb-admin --init_master_addrs=127.0.0.200:7100 --timeout_ms=300000 \
    upgrade_ysql_major_version_catalog
  echo run_ysql_catalog_upgrade finished at $(date +"%r")
}

# Restarts the masters in a mode that's aware of the PG11 to PG15 upgrade process, then runs ysql
# major upgrade to populate the pg15 catalog.
# Must be run while the current directory is a pg15 directory, typically the directory for the
# source code being built.
upgrade_masters_run_ysql_catalog_upgrade() {
  upgrade_masters
  run_ysql_catalog_upgrade
}

# Restart node 2 using PG15 binaries.
# Due to historic reasons we first restart the 2nd node instead of the 1st.
restart_node_2_in_pg15() {
  yb_ctl restart_node 2 --tserver_flags="$common_tserver_flags,$common_pg15_flags"
}

run_preflight_checks() {
  build/latest/postgres/bin/pg_upgrade --old-datadir "$data_dir/node-1/disk-1/pg_data" \
    --old-host "$PGHOST" --old-port 5433 --username "yugabyte" --check
}

finalize_upgrade() {
  build/latest/bin/yb-admin --init_master_addrs=127.0.0.200:7100 --timeout_ms=300000 \
    finalize_upgrade
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
