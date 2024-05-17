-- We're testing that the anon extension can be dumped and restored properly

CREATE DATABASE contrib_regression_test_restore TEMPLATE template0;

\! psql contrib_regression_test_restore -c 'CREATE EXTENSION anon CASCADE;'
\! psql contrib_regression_test_restore -c 'SELECT anon.start_dynamic_masking();'
\! pg_dump -Fc contrib_regression_test_restore > contrib_regression_test_restore.pgsql

DROP DATABASE contrib_regression_test_restore;

\! pg_restore -d postgres --role=postgres -C contrib_regression_test_restore.pgsql

DROP DATABASE contrib_regression_test_restore;
