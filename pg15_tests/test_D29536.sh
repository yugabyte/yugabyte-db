#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

yb_ctl_destroy_create
ysqlsh <<EOT
CREATE TABLE p1 (k INT PRIMARY KEY, v TEXT);
CREATE UNIQUE INDEX c1 ON p1 (v ASC) SPLIT AT VALUES (('foo'), ('qux'));
ALTER TABLE p1 ADD UNIQUE USING INDEX c1;
EOT
# TODO(19488): uncomment this
#build/latest/postgres/bin/ysql_dump -h $PGHOST --include-yb-metadata
