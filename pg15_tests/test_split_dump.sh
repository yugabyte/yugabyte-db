#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

yb_ctl_destroy_create
ysqlsh <<EOT
CREATE TABLE t1 (a int) SPLIT INTO 2 TABLETS;
CREATE TABLE t2 (k int, PRIMARY KEY (k ASC)) SPLIT AT VALUES ((100));
EOT
# TODO(19488): uncomment this
#build/latest/postgres/bin/ysql_dump -h $PGHOST --include-yb-metadata | grep -c 'SPLIT' | grep 2
