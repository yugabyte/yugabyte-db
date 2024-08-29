#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh
source "${BASH_SOURCE[0]%/*}"/common_upgrade.sh

run_and_pushd_pg11

# Create pre-existing PG11 table
ysqlsh <<EOT
SHOW server_version;
CREATE TABLE t (v INT);
INSERT INTO t VALUES (1),(2),(3);
CREATE MATERIALIZED VIEW mv AS SELECT * FROM t;
EOT

popd
upgrade_masters_run_ysql_catalog_upgrade

# With upgraded masters only, check on the status of the materialized view.
diff <(ysqlsh <<EOT | sed 's/ *$//'
SHOW server_version_num;
SELECT * FROM mv ORDER BY v;
EOT
) - <<EOT
 server_version_num
--------------------
 110002
(1 row)

 v
---
 1
 2
 3
(3 rows)

EOT

restart_node_2_in_pg15

# Test simultaneous use of the materialized view from both PG versions before the upgrade has been
# finalized.

# Usage from PG15
diff <(ysqlsh 2 <<EOT | sed 's/ *$//'
SHOW server_version_num;
SELECT * FROM mv ORDER BY v;
EOT
) - <<EOT
 server_version_num
--------------------
 150002
(1 row)

 v
---
 1
 2
 3
(3 rows)

EOT

# Usage from PG11
diff <(ysqlsh <<EOT | sed 's/ *$//'
SHOW server_version_num;
SELECT * FROM mv ORDER BY v;
EOT
) - <<EOT
 server_version_num
--------------------
 110002
(1 row)

 v
---
 1
 2
 3
(3 rows)

EOT

# Upgrade is complete. After the restart, verify everything still works.
yb_ctl restart
diff <(ysqlsh <<EOT | sed 's/ *$//'
SHOW server_version_num;
SELECT * FROM mv ORDER BY v;
INSERT INTO t VALUES (4);
REFRESH MATERIALIZED VIEW mv;
SELECT * FROM mv ORDER BY v;
EOT
) - <<EOT
 server_version_num
--------------------
 150002
(1 row)

 v
---
 1
 2
 3
(3 rows)

INSERT 0 1
REFRESH MATERIALIZED VIEW
 v
---
 1
 2
 3
 4
(4 rows)

EOT
