#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh
source "${BASH_SOURCE[0]%/*}"/common_upgrade.sh

run_and_pushd_pg11

# Store PG DB OIDs for later comparison
bin/yb-admin --master_addresses=$PGHOST:7100,$pghost2:7100,$pghost3:7100 list_namespaces \
  | grep 30008 | sort -k2 | awk '{print $1 " " $2}' > $data_dir/pg11_dbs.txt

# Create pre-existing PG11 table
ysqlsh <<EOT
SHOW server_version;
CREATE TABLE t (h int, r TEXT, v1 BIGINT, v2 VARCHAR, v3 JSONB, v4 int[], PRIMARY KEY (h, r));
INSERT INTO t VALUES (1, 'a', 5000000000, 'abc', '{"a" : 3.5}', '{1, 2, 3}'),
(1, 'b', -5000000000, 'def', '{"b" : 5}', '{1, 1, 2, 3}'),
(2, 'a', 5000000000, 'ghi', '{"c" : 30}', '{1, 4, 9}');
SELECT * FROM t;
EOT

popd

# Run preflight checks before upgrading yb-masters.
run_preflight_checks
upgrade_masters_run_ysql_catalog_upgrade

# Ensure that the PG15 initdb didn't create or modify namespace entries on the YB master.
diff $data_dir/pg11_dbs.txt <(build/latest/bin/yb-admin \
  --master_addresses=$PGHOST:7100,$pghost2:7100,$pghost3:7100 list_namespaces | grep 30008 \
  | sort -k2 | awk '{print $1 " " $2}')

restart_node_2_in_pg15

# Demonstrate simultaneous access for DMLs before the upgrade has been finalized. (DDLs are not
# allowed, and rollback to PG11 is still possible.)

# Insert from PG15
diff <(ysqlsh 2 <<EOT | sed 's/ *$//'
SHOW server_version_num;
INSERT INTO t VALUES (15, 'fifteen', cosh(0), '15', '{"num" : 15}', '{15}');
SELECT * FROM t ORDER BY h,r;
EOT
) - <<EOT
 server_version_num
--------------------
 150002
(1 row)

INSERT 0 1
 h  |    r    |     v1      | v2  |     v3      |    v4
----+---------+-------------+-----+-------------+-----------
  1 | a       |  5000000000 | abc | {"a": 3.5}  | {1,2,3}
  1 | b       | -5000000000 | def | {"b": 5}    | {1,1,2,3}
  2 | a       |  5000000000 | ghi | {"c": 30}   | {1,4,9}
 15 | fifteen |           1 | 15  | {"num": 15} | {15}
(4 rows)

EOT
# Insert from PG11, and note the PG15 insertion is visible
diff <(ysqlsh <<EOT | sed 's/ *$//'
SHOW server_version_num;
INSERT INTO t VALUES (11, 'eleven', 11, '11', '{"num": 11}', '{11}');
SELECT * FROM t ORDER BY h,r;
EOT
) - <<EOT
 server_version_num
--------------------
 110002
(1 row)

INSERT 0 1
 h  |    r    |     v1      | v2  |     v3      |    v4
----+---------+-------------+-----+-------------+-----------
  1 | a       |  5000000000 | abc | {"a": 3.5}  | {1,2,3}
  1 | b       | -5000000000 | def | {"b": 5}    | {1,1,2,3}
  2 | a       |  5000000000 | ghi | {"c": 30}   | {1,4,9}
 11 | eleven  |          11 | 11  | {"num": 11} | {11}
 15 | fifteen |           1 | 15  | {"num": 15} | {15}
(5 rows)

EOT

# Finalize the upgrade to re-enable pushdown and DDLs.
finalize_upgrade
# Upgrade is complete. After the restart, demonstrate that DDLs work.
yb_ctl restart

diff <(ysqlsh <<EOT | sed 's/ *$//'
SHOW server_version_num;
SELECT * FROM t ORDER BY h,r;
CREATE INDEX ON t (v1);
EXPLAIN (COSTS OFF) SELECT COUNT(*) FROM t WHERE v1 = 11;
SELECT COUNT(*) FROM t WHERE v1 = 11;
CREATE DATABASE userdb;
\connect userdb
CREATE TABLE t (a int);
INSERT INTO t VALUES (1);
SELECT * FROM t;
EOT
) - <<EOT
 server_version_num
--------------------
 150002
(1 row)

 h  |    r    |     v1      | v2  |     v3      |    v4
----+---------+-------------+-----+-------------+-----------
  1 | a       |  5000000000 | abc | {"a": 3.5}  | {1,2,3}
  1 | b       | -5000000000 | def | {"b": 5}    | {1,1,2,3}
  2 | a       |  5000000000 | ghi | {"c": 30}   | {1,4,9}
 11 | eleven  |          11 | 11  | {"num": 11} | {11}
 15 | fifteen |           1 | 15  | {"num": 15} | {15}
(5 rows)

CREATE INDEX
                QUERY PLAN
-------------------------------------------
 Finalize Aggregate
   ->  Index Only Scan using t_v1_idx on t
         Index Cond: (v1 = 11)
         Partial Aggregate: true
(4 rows)

 count
-------
     1
(1 row)

CREATE DATABASE
You are now connected to database "userdb" as user "yugabyte".
CREATE TABLE
INSERT 0 1
 a
---
 1
(1 row)

EOT
