#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh
source "${BASH_SOURCE[0]%/*}"/common_upgrade.sh

run_and_pushd_pg11

# Create a pre-existing PG11 table
ysqlsh <<EOT
SHOW server_version;
CREATE TABLE t (a int);
EOT

popd
upgrade_masters_run_ysql_catalog_upgrade

# Run \d from a PG11 tserver. Because the catalog tablets are on the master and the masters are
# running PG15, this exercises expression pushdown across PG versions if enabled.
diff <(ysqlsh <<EOT | sed 's/ *$//'
SHOW server_version;
SHOW yb_enable_expression_pushdown;
\d
EOT
) - <<EOT
   server_version
---------------------
 11.2-YB-2.20.2.2-b0
(1 row)

 yb_enable_expression_pushdown
-------------------------------
 off
(1 row)

        List of relations
 Schema | Name | Type  |  Owner
--------+------+-------+----------
 public | t    | table | yugabyte
(1 row)

EOT
