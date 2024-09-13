#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh
source "${BASH_SOURCE[0]%/*}"/common_upgrade.sh

run_and_pushd_pg11

# Create pre-existing PG11 tables in different databases
ysqlsh <<EOT
SHOW server_version;
\connect system_platform
CREATE TABLE t (v INT);
INSERT INTO t VALUES (1);
\connect postgres
CREATE TABLE t (v INT);
INSERT INTO t VALUES (10);
CREATE DATABASE userdb;
\connect userdb
CREATE TABLE t (v INT);
INSERT INTO t VALUES (100);
EOT

popd
upgrade_masters_run_ysql_catalog_upgrade
restart_node_2_in_pg15

# Test simultaneous use of the databases from both PG versions before the upgrade has been
# finalized.

# Usage from PG15
diff <(ysqlsh 2 <<EOT | sed 's/ *$//'
SHOW server_version_num;
\connect system_platform
INSERT INTO t VALUES (2);
SELECT * FROM t ORDER BY v;
\connect postgres
INSERT INTO t VALUES (20);
SELECT * FROM t ORDER BY v;
\connect userdb
INSERT INTO t VALUES (200);
SELECT * FROM t ORDER BY v;
EOT
) - <<EOT
 server_version_num
--------------------
 150002
(1 row)

You are now connected to database "system_platform" as user "yugabyte".
INSERT 0 1
 v
---
 1
 2
(2 rows)

You are now connected to database "postgres" as user "yugabyte".
INSERT 0 1
 v
----
 10
 20
(2 rows)

You are now connected to database "userdb" as user "yugabyte".
INSERT 0 1
  v
-----
 100
 200
(2 rows)

EOT

# Usage from PG11
diff <(ysqlsh <<EOT | sed 's/ *$//'
SHOW server_version_num;
\connect system_platform
INSERT INTO t VALUES (3);
SELECT * FROM t ORDER BY v;
\connect postgres
INSERT INTO t VALUES (30);
SELECT * FROM t ORDER BY v;
\connect userdb
INSERT INTO t VALUES (300);
SELECT * FROM t ORDER BY v;
EOT
) - <<EOT
 server_version_num
--------------------
 110002
(1 row)

You are now connected to database "system_platform" as user "yugabyte".
INSERT 0 1
 v
---
 1
 2
 3
(3 rows)

You are now connected to database "postgres" as user "yugabyte".
INSERT 0 1
 v
----
 10
 20
 30
(3 rows)

You are now connected to database "userdb" as user "yugabyte".
INSERT 0 1
  v
-----
 100
 200
 300
(3 rows)

EOT

# Upgrade is complete. After the restart, verify everything still works.
yb_ctl restart
diff <(ysqlsh <<EOT | sed 's/ *$//'
SHOW server_version_num;
\connect system_platform
INSERT INTO t VALUES (4);
SELECT * FROM t ORDER BY v;
\connect postgres
INSERT INTO t VALUES (40);
SELECT * FROM t ORDER BY v;
\connect userdb
INSERT INTO t VALUES (400);
SELECT * FROM t ORDER BY v;
EOT
) - <<EOT
 server_version_num
--------------------
 150002
(1 row)

You are now connected to database "system_platform" as user "yugabyte".
INSERT 0 1
 v
---
 1
 2
 3
 4
(4 rows)

You are now connected to database "postgres" as user "yugabyte".
INSERT 0 1
 v
----
 10
 20
 30
 40
(4 rows)

You are now connected to database "userdb" as user "yugabyte".
INSERT 0 1
  v
-----
 100
 200
 300
 400
(4 rows)

EOT
