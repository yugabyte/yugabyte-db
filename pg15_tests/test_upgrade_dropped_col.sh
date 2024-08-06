#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh
source "${BASH_SOURCE[0]%/*}"/common_upgrade.sh

run_and_pushd_pg11

# Create pre-existing PG11 table
ysqlsh <<EOT
CREATE TABLE t (a int, b int, c int);
INSERT INTO t VALUES (10, 20, 30), (100, 200, 300);
ALTER TABLE t DROP COLUMN b;
ALTER TABLE t ADD COLUMN d int;
ALTER TABLE t ADD COLUMN e text DEFAULT 'foo';
INSERT INTO t VALUES (1000, 3000, 4000);
SELECT attname, attnum FROM pg_attribute WHERE attrelid = 't'::pg_catalog.regclass AND attnum >= 0;

CREATE TABLE t2 (a2 int, b2 int, c2 int, d2 int, PRIMARY KEY(d2 HASH, a2 ASC));
ALTER TABLE t2 DROP COLUMN b2;
INSERT INTO t2 VALUES (2, 2000, 1);
SELECT attname, attnum FROM pg_attribute WHERE attrelid = 't2'::pg_catalog.regclass AND attnum >= 0;
EOT

popd
upgrade_masters_run_ysql_catalog_upgrade

restart_node_2_in_pg15

# Demonstrate simultaneous access for DMLs involving the dropped and added columns, before the
# upgrade has been finalized.

# Insert from PG15
diff <(ysqlsh 2 <<EOT | sed 's/ *$//'
SHOW server_version_num;
INSERT INTO t VALUES (15, 150, 1500);
SELECT * FROM t ORDER BY c;
SELECT attname, attnum FROM pg_attribute WHERE attrelid = 't'::pg_catalog.regclass AND attnum >= 0;

SELECT * FROM t2;
SELECT attname, attnum FROM pg_attribute WHERE attrelid = 't2'::pg_catalog.regclass AND attnum >= 0;
EOT
) - <<EOT
 server_version_num
--------------------
 150002
(1 row)

INSERT 0 1
  a   |  c   |  d   |  e
------+------+------+-----
   10 |   30 |      | foo
   15 |  150 | 1500 | foo
  100 |  300 |      | foo
 1000 | 3000 | 4000 | foo
(4 rows)

           attname            | attnum
------------------------------+--------
 a                            |      1
 ........pg.dropped.2........ |      2
 c                            |      3
 d                            |      4
 e                            |      5
(5 rows)

 a2 |  c2  | d2
----+------+----
  2 | 2000 |  1
(1 row)

           attname            | attnum
------------------------------+--------
 a2                           |      1
 ........pg.dropped.2........ |      2
 c2                           |      3
 d2                           |      4
(4 rows)

EOT
# Insert from PG11, and note the PG15 insertion is visible
diff <(ysqlsh <<EOT | sed 's/ *$//'
SHOW server_version_num;
INSERT INTO t VALUES (11, 110, 1100);
SELECT * FROM t ORDER BY c;
SELECT attname, attnum FROM pg_attribute WHERE attrelid = 't'::pg_catalog.regclass AND attnum >= 0;

-- Note double output here due to ALTER TABLE public.t OWNER TO yugabyte; on another node, it's done
-- as part of pg_restore, but the bug is present on the pg15 branch as well if you execute this
-- statement on another node.
SELECT * FROM t2;
SELECT attname, attnum FROM pg_attribute WHERE attrelid = 't2'::pg_catalog.regclass AND attnum >= 0;
EOT
) - <<EOT
 server_version_num
--------------------
 110002
(1 row)

INSERT 0 1
  a   |  c   |  d   |  e
------+------+------+-----
   10 |   30 |      | foo
   11 |  110 | 1100 | foo
   15 |  150 | 1500 | foo
  100 |  300 |      | foo
 1000 | 3000 | 4000 | foo
(5 rows)

           attname            | attnum
------------------------------+--------
 a                            |      1
 ........pg.dropped.2........ |      2
 c                            |      3
 d                            |      4
 e                            |      5
(5 rows)

 a2 | c2 | d2
----+----+----
(0 rows)

 a2 |  c2  | d2
----+------+----
  2 | 2000 |  1
(1 row)

           attname            | attnum
------------------------------+--------
 a2                           |      1
 ........pg.dropped.2........ |      2
 c2                           |      3
 d2                           |      4
(4 rows)

EOT
