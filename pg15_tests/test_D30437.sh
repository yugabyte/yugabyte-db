#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

yb_ctl_destroy_create
bin/ysqlsh -X -v "ON_ERROR_STOP=1" <<EOT
CREATE TABLE t (k int PRIMARY KEY);
EOT

[ $(build/latest/postgres/bin/ysql_dump -h $PGHOST --schema-only --binary-upgrade \
  --include-yb-metadata \
  | grep -E "binary_upgrade_set_next_index_pg_class_oid|binary_upgrade_set_next_index_relfilenode" \
  | wc -l) -eq 2 ]
