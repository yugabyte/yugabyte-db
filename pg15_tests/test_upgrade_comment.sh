#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh
source "${BASH_SOURCE[0]%/*}"/common_upgrade.sh

run_and_pushd_pg11

# Create pre-existing PG11 table and comment on database yugabyte and table t
ysqlsh <<EOT
SHOW server_version;
CREATE TABLE t (a int);
COMMENT ON DATABASE yugabyte IS 'Comment made on db yugabyte in PG11';
COMMENT ON TABLE t IS 'Comment made on table t in PG11';
EOT

popd
upgrade_masters_run_ysql_catalog_upgrade
restart_node_2_in_pg15

# Check comment in PG15 during binary upgrade
diff <(ysqlsh 2 <<EOT | sed 's/ *$//'
SHOW server_version_num;
SELECT pg_catalog.shobj_description(d.oid, 'pg_database')
FROM   pg_catalog.pg_database d
WHERE  datname = 'yugabyte';
SELECT description from pg_description
JOIN pg_class on pg_description.objoid = pg_class.oid
WHERE relname = 't';
EOT
) - <<EOT
 server_version_num
--------------------
 150002
(1 row)

          shobj_description
-------------------------------------
 Comment made on db yugabyte in PG11
(1 row)

           description
---------------------------------
 Comment made on table t in PG11
(1 row)

EOT

# Check comment in PG11 during binary upgrade
diff <(ysqlsh <<EOT | sed 's/ *$//'
SHOW server_version_num;
SELECT pg_catalog.shobj_description(d.oid, 'pg_database')
FROM   pg_catalog.pg_database d
WHERE  datname = 'yugabyte';
SELECT description from pg_description
JOIN pg_class on pg_description.objoid = pg_class.oid
WHERE relname = 't';
EOT
) - <<EOT
 server_version_num
--------------------
 110002
(1 row)

          shobj_description
-------------------------------------
 Comment made on db yugabyte in PG11
(1 row)

           description
---------------------------------
 Comment made on table t in PG11
(1 row)

EOT

# Upgrade is complete. After the restart, check comment on database and table is
# not modified after upgrade
yb_ctl restart
diff <(ysqlsh <<EOT | sed 's/ *$//'
SELECT pg_catalog.shobj_description(d.oid, 'pg_database')
FROM   pg_catalog.pg_database d
WHERE  datname = 'yugabyte';
SELECT description from pg_description
JOIN pg_class on pg_description.objoid = pg_class.oid
WHERE relname = 't';
EOT
) - <<EOT
          shobj_description
-------------------------------------
 Comment made on db yugabyte in PG11
(1 row)

           description
---------------------------------
 Comment made on table t in PG11
(1 row)

EOT

# Modify comment after upgrade to PG15
ysqlsh <<EOT
COMMENT ON DATABASE yugabyte IS 'Comment made on db yugabyte in PG15';
COMMENT ON TABLE t IS 'Comment made on table t in PG15';
EOT

# Check comment is modified
diff <(ysqlsh <<EOT | sed 's/ *$//'
SELECT pg_catalog.shobj_description(d.oid, 'pg_database')
FROM   pg_catalog.pg_database d
WHERE  datname = 'yugabyte';
SELECT description from pg_description
JOIN pg_class on pg_description.objoid = pg_class.oid
WHERE relname = 't';
EOT
) - <<EOT
          shobj_description
-------------------------------------
 Comment made on db yugabyte in PG15
(1 row)

           description
---------------------------------
 Comment made on table t in PG15
(1 row)

EOT
