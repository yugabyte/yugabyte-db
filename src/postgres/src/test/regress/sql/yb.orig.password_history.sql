-- pg_yb_password_history is not publicly readable.

SELECT has_table_privilege('pg_catalog.pg_yb_password_history', 'SELECT') AS superuser_select;
SELECT has_table_privilege('public', 'pg_catalog.pg_yb_password_history', 'SELECT') AS public_select;

SET yb_non_ddl_txn_for_sys_tables_allowed TO on;
INSERT INTO pg_catalog.pg_yb_password_history (pwdhstrole, pwdhstchangetime, pwdhstpassword)
VALUES (123456, '2000-01-01 00:00:00+00', 'mockhash');
RESET yb_non_ddl_txn_for_sys_tables_allowed;

-- A superuser can still read it.
SELECT count(*) FROM pg_catalog.pg_yb_password_history;

-- A non-superuser must be denied access to the catalog.
CREATE USER pwdhist_reader;
\c yugabyte pwdhist_reader
SELECT has_table_privilege('pg_catalog.pg_yb_password_history', 'SELECT') AS my_select;
SELECT * FROM pg_catalog.pg_yb_password_history;

\c yugabyte yugabyte
SET yb_non_ddl_txn_for_sys_tables_allowed TO on;
DELETE FROM pg_catalog.pg_yb_password_history WHERE pwdhstrole=123456;
RESET yb_non_ddl_txn_for_sys_tables_allowed;
DROP USER pwdhist_reader;
