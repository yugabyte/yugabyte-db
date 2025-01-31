#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

yb_ctl_destroy_create

ysqlsh <<EOT
SET yb_debug_log_docdb_requests = ON;
CREATE FUNCTION trigger_func() RETURNS trigger LANGUAGE plpgsql AS '
BEGIN
    RETURN NULL;
END;';
CREATE TABLE trigger_tab (i int);
CREATE TRIGGER trigger
    AFTER UPDATE ON trigger_tab
    FOR EACH ROW EXECUTE PROCEDURE trigger_func();
INSERT INTO trigger_tab VALUES (1);
EOT
grep Flushing "$data_dir"/node-1/disk-1/yb-data/tserver/logs/postgresql-*.log \
  | tail -1 \
  | grep "Flushing buffered operations, using transactional session (num ops: 1)"

ysqlsh <<EOT
SET yb_debug_log_docdb_requests = ON;
CREATE TABLE index_tab (i int, unique (i));
INSERT INTO index_tab VALUES (1);
EOT
grep Flushing "$data_dir"/node-1/disk-1/yb-data/tserver/logs/postgresql-*.log \
  | tail -1 \
  | grep "Flushing buffered operations, using transactional session (num ops: 2)"
