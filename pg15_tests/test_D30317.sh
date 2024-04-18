#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

yb_ctl_destroy_create
ysqlsh <<EOT
CREATE TABLE t (a int);
EOT
# Test that the catalog table yugabyte.pg_class has the version field 01 (PG15), and the user table
# yugabyte.t has the version field 00, because user tables are versionless.
# The first 8 hex digits of each UUID represent the OID of the yugabyte namespace, which may change
# depending on how many objects are initialized before it. Therefore, we mask them.
diff <(build/latest/bin/yb-admin --master_addresses=$PGHOST:7100 list_tables include_table_id \
  | grep -E "^(yugabyte.t|yugabyte.pg_class) " | sort | sed 's/ ......../ xxxxxxxx/') - <<EOT
yugabyte.pg_class xxxxxxxx0000300080010000000004eb
yugabyte.t xxxxxxxx000030008000000000004000
EOT
