SET pgaudit.log_client = ON;
SET pgaudit.log_level = 'notice';
SET pgaudit.log_parameter = ON;

-- Testing if setting pgaudit.log is audited. We perform an early bail out when logging is disabled.
-- When we are trying to set pgaudit.log this set statement should be logged although the state
-- of the current system is such that logging is disable. We test that corner case here.
SET pgaudit.log = 'NONE';
SET pgaudit.log = 'ALL';

-- A role password sent in a multi-statement query must be redacted in the
-- records of every statement of that query, not only the role statement's.
CREATE ROLE regress_yb_pw_role LOGIN;
SELECT 1 \; ALTER ROLE regress_yb_pw_role PASSWORD 'pw_middle' \; SELECT 2;
ALTER ROLE regress_yb_pw_role PASSWORD 'pw_first' \; SELECT 3;
BEGIN \; ALTER ROLE regress_yb_pw_role PASSWORD 'pw_txn' \; COMMIT;
CREATE ROLE regress_yb_pw_role2 PASSWORD 'pw_create' \; GRANT regress_yb_pw_role2 TO regress_yb_pw_role;
SELECT query FROM pg_stat_activity WHERE pid = pg_backend_pid() \; ALTER ROLE regress_yb_pw_role PASSWORD 'pw_activity';
-- Without a role statement, text containing the password token is kept.
SELECT 'password' AS col \; SELECT 4;
DO $$ BEGIN EXECUTE 'ALTER ROLE regress_yb_pw_role PASSWORD ''pw_do'''; END $$;
CREATE FOREIGN DATA WRAPPER regress_yb_pw_fdw;
CREATE SERVER regress_yb_pw_srv FOREIGN DATA WRAPPER regress_yb_pw_fdw;
CREATE USER MAPPING FOR regress_yb_pw_role SERVER regress_yb_pw_srv OPTIONS (user 'u', password 'pw_um');
ALTER USER MAPPING FOR regress_yb_pw_role SERVER regress_yb_pw_srv OPTIONS (SET password 'pw_um_alter');
DROP SERVER regress_yb_pw_srv CASCADE;
DROP FOREIGN DATA WRAPPER regress_yb_pw_fdw;
DROP ROLE regress_yb_pw_role2;
DROP ROLE regress_yb_pw_role;
