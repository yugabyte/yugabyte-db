#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh
source "${BASH_SOURCE[0]%/*}"/common_upgrade.sh

run_and_pushd_pg11

# Create pre-existing PG11 partitioned tables
ysqlsh <<EOT
SHOW server_version;
CREATE TABLE t_r (v INT) PARTITION BY RANGE (v);
CREATE TABLE t_h (v INT) PARTITION BY HASH (v);
CREATE TABLE t_l (v INT) PARTITION BY LIST (v);

CREATE TABLE t_r_1 PARTITION OF t_r FOR VALUES FROM (1) TO (3);
CREATE TABLE t_r_default PARTITION OF t_r DEFAULT;

-- hash partitioned tables are partitioned based on the hash value of the
-- partition key, not the key itself
CREATE TABLE t_h_1 PARTITION OF t_h FOR VALUES WITH (MODULUS 2, REMAINDER 0);
CREATE TABLE t_h_2 PARTITION OF t_h FOR VALUES WITH (MODULUS 2, REMAINDER 1);

CREATE TABLE t_l_1 PARTITION OF t_l FOR VALUES IN (1, 2);
CREATE TABLE t_l_default PARTITION OF t_l DEFAULT;

INSERT INTO t_r VALUES (1),(2),(3);
INSERT INTO t_h VALUES (1),(2),(3);
INSERT INTO t_l VALUES (1),(2),(3);
EOT

popd
upgrade_masters_run_ysql_catalog_upgrade

RANGE_PARTITIONED_QUERIES=$(cat << EOT
SELECT * FROM t_r ORDER BY v;
SELECT * FROM t_r_1 ORDER BY v;
SELECT * FROM t_r_default ORDER BY v;
EOT
)

HASH_PARTITIONED_QUERIES=$(cat << EOT
SELECT * FROM t_h ORDER BY v;
SELECT * FROM t_h_1 ORDER BY v;
SELECT * FROM t_h_2 ORDER BY v;
EOT
)

VALUE_PARTITIONED_QUERIES=$(cat << EOT
SELECT * FROM t_l ORDER BY v;
SELECT * FROM t_l_1 ORDER BY v;
SELECT * FROM t_l_default ORDER BY v;
EOT
)

EXPECTED_OUTPUT_1_2_3=$(cat << EOT
 v
---
 1
 2
 3
(3 rows)

 v
---
 1
 2
(2 rows)

 v
---
 3
(1 row)
EOT
)

EXPECTED_OUTPUT_1_2_3_4=$(cat << EOT
 v
---
 1
 2
 3
 4
(4 rows)

 v
---
 1
 2
(2 rows)

 v
---
 3
 4
(2 rows)
EOT
)

# With upgraded masters only, check each of the tables
diff <(ysqlsh <<EOT | sed 's/ *$//'
SHOW server_version_num;
$RANGE_PARTITIONED_QUERIES
$HASH_PARTITIONED_QUERIES
$VALUE_PARTITIONED_QUERIES
EOT
) - <<EOT
 server_version_num
--------------------
 110002
(1 row)

$EXPECTED_OUTPUT_1_2_3

$EXPECTED_OUTPUT_1_2_3

$EXPECTED_OUTPUT_1_2_3

EOT

restart_node_2_in_pg15

# Test simultaneous use of the partitioned tables from both PG versions before
# the upgrade has been finalized.

# Usage from PG15
diff <(ysqlsh 2 <<EOT | sed 's/ *$//'
SHOW server_version_num;
INSERT INTO t_r VALUES (4);
INSERT INTO t_h VALUES (4);
INSERT INTO t_l VALUES (4);
$RANGE_PARTITIONED_QUERIES
$HASH_PARTITIONED_QUERIES
$VALUE_PARTITIONED_QUERIES
EOT
) - <<EOT
 server_version_num
--------------------
 150002
(1 row)

INSERT 0 1
INSERT 0 1
INSERT 0 1
$EXPECTED_OUTPUT_1_2_3_4

$EXPECTED_OUTPUT_1_2_3_4

$EXPECTED_OUTPUT_1_2_3_4

EOT

# Usage from PG11
diff <(ysqlsh <<EOT | sed 's/ *$//'
SHOW server_version_num;
$RANGE_PARTITIONED_QUERIES
$HASH_PARTITIONED_QUERIES
$VALUE_PARTITIONED_QUERIES
DELETE FROM t_r WHERE v = 4;
DELETE FROM t_h WHERE v = 4;
DELETE FROM t_l WHERE v = 4;
$RANGE_PARTITIONED_QUERIES
$HASH_PARTITIONED_QUERIES
$VALUE_PARTITIONED_QUERIES
EOT
) - <<EOT
 server_version_num
--------------------
 110002
(1 row)

$EXPECTED_OUTPUT_1_2_3_4

$EXPECTED_OUTPUT_1_2_3_4

$EXPECTED_OUTPUT_1_2_3_4

DELETE 1
DELETE 1
DELETE 1
$EXPECTED_OUTPUT_1_2_3

$EXPECTED_OUTPUT_1_2_3

$EXPECTED_OUTPUT_1_2_3

EOT

# Upgrade is complete. After the restart, verify everything still works.
yb_ctl restart
diff <(ysqlsh <<EOT | sed 's/ *$//'
SHOW server_version_num;
CREATE TABLE t_r_2 PARTITION OF t_r FOR VALUES FROM (4) TO (6);
CREATE TABLE t_l_2 PARTITION OF t_l FOR VALUES IN (4, 5);
INSERT INTO t_r VALUES (4),(7);
INSERT INTO t_h VALUES (4),(7);
INSERT INTO t_l VALUES (4),(7);
$RANGE_PARTITIONED_QUERIES
SELECT * FROM t_r_2 ORDER BY v;
$HASH_PARTITIONED_QUERIES
$VALUE_PARTITIONED_QUERIES
SELECT * FROM t_l_2 ORDER BY v;
EOT
) - <<EOT
 server_version_num
--------------------
 150002
(1 row)

CREATE TABLE
CREATE TABLE
INSERT 0 2
INSERT 0 2
INSERT 0 2
 v
---
 1
 2
 3
 4
 7
(5 rows)

 v
---
 1
 2
(2 rows)

 v
---
 3
 7
(2 rows)

 v
---
 4
(1 row)

 v
---
 1
 2
 3
 4
 7
(5 rows)

 v
---
 1
 2
(2 rows)

 v
---
 3
 4
 7
(3 rows)

 v
---
 1
 2
 3
 4
 7
(5 rows)

 v
---
 1
 2
(2 rows)

 v
---
 3
 7
(2 rows)

 v
---
 4
(1 row)

EOT
