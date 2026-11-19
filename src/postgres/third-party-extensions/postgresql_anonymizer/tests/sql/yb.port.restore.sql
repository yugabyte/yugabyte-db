-- We're testing that the anon extension can be dumped and restored properly

CREATE DATABASE contrib_regression_test_restore TEMPLATE template0;

\! ${YB_BUILD_ROOT}/postgres/bin/ysqlsh contrib_regression_test_restore -c 'CREATE EXTENSION anon CASCADE;' # YB: Use ysqlsh
-- YB: anon 3.x has no anon.start_dynamic_masking(), so the upstream line below is commented out and transparent dynamic masking is enabled instead
-- \! psql contrib_regression_test_restore -c 'SELECT anon.start_dynamic_masking();'
\! ${YB_BUILD_ROOT}/postgres/bin/ysqlsh contrib_regression_test_restore -c 'ALTER DATABASE contrib_regression_test_restore SET anon.transparent_dynamic_masking TO true;' # YB
\! ${YB_BUILD_ROOT}/postgres/bin/ysql_dump -h ${PGHOST} -Fc contrib_regression_test_restore > contrib_regression_test_restore.pgsql # YB: Use ysql_dump with the correct server

DROP DATABASE contrib_regression_test_restore;

\! ${YB_BUILD_ROOT}/postgres/bin/pg_restore -d postgres --role=postgres -C contrib_regression_test_restore.pgsql # YB: Fix path for pg_restore

DROP DATABASE contrib_regression_test_restore;
