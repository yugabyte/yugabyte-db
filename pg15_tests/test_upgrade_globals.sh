#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh
source "${BASH_SOURCE[0]%/*}"/common_upgrade.sh

run_and_pushd_pg11

# Create pre-existing PG11 cluster with various global objects
ysqlsh <<EOT
SHOW server_version;
-- Roles
CREATE ROLE alice LOGIN;
CREATE ROLE bob LOGIN;

-- Role membership
CREATE ROLE carol IN ROLE alice LOGIN;

\c yugabyte alice
CREATE TABLE t (a int);

-- Tablespace
\c yugabyte yugabyte
CREATE TABLESPACE ts LOCATION '/invalid';
CREATE TABLE t2 (b int) TABLESPACE ts;
EOT

popd
upgrade_masters_run_ysql_catalog_upgrade
restart_node_2_in_pg15

# Check that from pg15, we can connect as alice, bob, and carol. alice can see her own table. carol
# can also see it, with her role membership in role alice. bob cannot see alice's table.
# Also check that table t2 is in the correct tablespace.
diff <(ysqlsh 2 -v "ON_ERROR_STOP=0" <<EOT | sed 's/ *$//'
SHOW server_version_num;
\c yugabyte alice
SELECT * FROM t;
\c yugabyte bob
SELECT * FROM t;
\c yugabyte carol
SELECT * FROM t;
\c yugabyte yugabyte
SELECT * FROM t2;
SELECT ts.spcname
  FROM pg_tablespace ts
  INNER JOIN pg_class c ON ts.oid = c.reltablespace
  WHERE c.oid='t2'::regclass;
EOT
) - <<EOT
 server_version_num
--------------------
 150002
(1 row)

You are now connected to database "yugabyte" as user "alice".
 a
---
(0 rows)

You are now connected to database "yugabyte" as user "bob".
ERROR:  permission denied for table t
You are now connected to database "yugabyte" as user "carol".
 a
---
(0 rows)

You are now connected to database "yugabyte" as user "yugabyte".
 b
---
(0 rows)

 spcname
---------
 ts
(1 row)

EOT

# Check the same from pg11 in mixed mode
diff <(ysqlsh -v "ON_ERROR_STOP=0" <<EOT | sed 's/ *$//'
SHOW server_version_num;
\c yugabyte alice
SELECT * FROM t;
\c yugabyte bob
SELECT * FROM t;
\c yugabyte carol
SELECT * FROM t;
\c yugabyte yugabyte
SELECT * FROM t2;
SELECT ts.spcname
  FROM pg_tablespace ts
  INNER JOIN pg_class c ON ts.oid = c.reltablespace
  WHERE c.oid='t2'::regclass;
EOT
) - <<EOT
 server_version_num
--------------------
 110002
(1 row)

You are now connected to database "yugabyte" as user "alice".
 a
---
(0 rows)

You are now connected to database "yugabyte" as user "bob".
ERROR:  permission denied for table t
You are now connected to database "yugabyte" as user "carol".
 a
---
(0 rows)

You are now connected to database "yugabyte" as user "yugabyte".
 b
---
(0 rows)

 spcname
---------
 ts
(1 row)

EOT

# Upgrade is complete. After the restart, check the same conditions.
yb_ctl restart
diff <(ysqlsh -v "ON_ERROR_STOP=0" <<EOT | sed 's/ *$//'
\c yugabyte alice
SELECT * FROM t;
\c yugabyte bob
SELECT * FROM t;
\c yugabyte carol
SELECT * FROM t;
\c yugabyte yugabyte
SELECT * FROM t2;
SELECT ts.spcname
  FROM pg_tablespace ts
  INNER JOIN pg_class c ON ts.oid = c.reltablespace
  WHERE c.oid='t2'::regclass;
EOT
) - <<EOT
You are now connected to database "yugabyte" as user "alice".
 a
---
(0 rows)

You are now connected to database "yugabyte" as user "bob".
ERROR:  permission denied for table t
You are now connected to database "yugabyte" as user "carol".
 a
---
(0 rows)

You are now connected to database "yugabyte" as user "yugabyte".
 b
---
(0 rows)

 spcname
---------
 ts
(1 row)

EOT
