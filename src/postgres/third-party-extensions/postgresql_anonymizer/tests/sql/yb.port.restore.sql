-- We're testing that the anon extension can be dumped and restored properly

CREATE DATABASE contrib_regression_test_restore TEMPLATE template0;

\! ${YB_BUILD_ROOT}/postgres/bin/ysqlsh contrib_regression_test_restore -c 'CREATE EXTENSION anon CASCADE;' # YB: Use ysqlsh
\! ${YB_BUILD_ROOT}/postgres/bin/ysqlsh contrib_regression_test_restore -c 'BEGIN; SET yb_non_ddl_txn_for_sys_tables_allowed = true; SELECT anon.start_dynamic_masking(); COMMIT;' # YB: Use ysqlsh, wrap the anon.start_dynamic_masking() call in a transaction block with yb_non_ddl_txn_for_sys_tables_allowed on
\! ${YB_BUILD_ROOT}/postgres/bin/ysql_dump -h ${PGHOST} -Fc contrib_regression_test_restore > contrib_regression_test_restore.pgsql # YB: Use ysql_dump with the correct server

DROP DATABASE contrib_regression_test_restore;

\! ${YB_BUILD_ROOT}/postgres/bin/pg_restore -d postgres --role=postgres -C contrib_regression_test_restore.pgsql # YB: Fix path for pg_restore

DROP DATABASE contrib_regression_test_restore;
