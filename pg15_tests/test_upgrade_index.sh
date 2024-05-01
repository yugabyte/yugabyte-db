#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh
source "${BASH_SOURCE[0]%/*}"/common_upgrade.sh

run_and_pushd_pg11

# Create pre-existing PG11 table
ysqlsh <<EOT
SHOW server_version;
CREATE TABLE t (a int);
CREATE INDEX on t (a);
INSERT INTO t VALUES (1),(2),(3),(4),(5);
SELECT * FROM t ORDER BY a;
EOT

popd
upgrade_masters_run_initdb

ysql_upgrade_using_node_2

# Test simultaneous use of the index from both PG versions before the upgrade has been finalized.

# Insert and usage from PG15
diff <(ysqlsh 2 <<EOT | sed 's/ *$//'
SHOW server_version_num;
INSERT INTO t VALUES (15);
SELECT * FROM t WHERE a = 15;
EXPLAIN (COSTS OFF) SELECT * FROM t WHERE a = 15;
EOT
) - <<EOT
 server_version_num
--------------------
 150002
(1 row)

INSERT 0 1
 a
----
 15
(1 row)

             QUERY PLAN
------------------------------------
 Index Only Scan using t_a_idx on t
   Index Cond: (a = 15)
(2 rows)

EOT

# Insert from PG11, and note the PG15 insertion is visible
diff <(ysqlsh <<EOT | sed 's/ *$//'
SHOW server_version_num;
INSERT INTO t VALUES (11);
SELECT * FROM t WHERE a = 15;
EXPLAIN (COSTS OFF) SELECT * FROM t WHERE a = 15;
SELECT * FROM t WHERE a = 11;
EOT
) - <<EOT
 server_version_num
--------------------
 110002
(1 row)

INSERT 0 1
 a
----
 15
(1 row)

             QUERY PLAN
------------------------------------
 Index Only Scan using t_a_idx on t
   Index Cond: (a = 15)
(2 rows)

 a
----
 11
(1 row)

EOT

# Upgrade is complete. After the restart, verify everything looks right.
yb_ctl restart
diff <(ysqlsh <<EOT | sed 's/ *$//'
SHOW server_version_num;
SELECT * FROM t ORDER BY a;
SELECT * FROM t WHERE a = 1;
SELECT * FROM t WHERE a = 11;
SELECT * FROM t WHERE a = 15;
EXPLAIN (COSTS OFF) SELECT * FROM t WHERE a = 1;
EXPLAIN (COSTS OFF) SELECT * FROM t WHERE a = 11;
EXPLAIN (COSTS OFF) SELECT * FROM t WHERE a = 15;
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
  3
  4
  5
 11
 15
(7 rows)

 a
---
 1
(1 row)

 a
----
 11
(1 row)

 a
----
 15
(1 row)

             QUERY PLAN
------------------------------------
 Index Only Scan using t_a_idx on t
   Index Cond: (a = 1)
(2 rows)

             QUERY PLAN
------------------------------------
 Index Only Scan using t_a_idx on t
   Index Cond: (a = 11)
(2 rows)

             QUERY PLAN
------------------------------------
 Index Only Scan using t_a_idx on t
   Index Cond: (a = 15)
(2 rows)

EOT
