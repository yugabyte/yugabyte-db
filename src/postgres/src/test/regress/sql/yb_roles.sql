--
-- Test yb_extension role
--
CREATE USER regress_priv_user;
CREATE OPERATOR FAMILY alt_opf1 USING hash;
SET SESSION AUTHORIZATION regress_priv_user;
CREATE EXTENSION pgcrypto; -- should fail
CREATE EXTENSION orafce; -- should fail
\c -
GRANT yb_extension TO regress_priv_user;
SET SESSION AUTHORIZATION regress_priv_user;
CREATE EXTENSION pgcrypto;
ALTER EXTENSION pgcrypto UPDATE TO '1.3';
DROP EXTENSION pgcrypto;
CREATE EXTENSION pg_trgm WITH VERSION '1.3';
ALTER EXTENSION pg_trgm UPDATE TO '1.4';
DROP EXTENSION pg_trgm;
ALTER OPERATOR FAMILY alt_opf1 USING hash ADD -- should fail
  OPERATOR 1 < (int1, int2);
\c -
DROP USER regress_priv_user;

--
-- Test yb_fdw role
--
CREATE USER regress_priv_user1;
CREATE USER regress_priv_user2;
SET SESSION AUTHORIZATION regress_priv_user1;
CREATE FOREIGN DATA WRAPPER useless; -- should fail
\c -
GRANT yb_fdw TO regress_priv_user1 WITH ADMIN OPTION;
GRANT yb_extension TO regress_priv_user1;

SET SESSION AUTHORIZATION regress_priv_user1;
CREATE FOREIGN DATA WRAPPER useless;
ALTER FOREIGN DATA WRAPPER useless NO VALIDATOR;
ALTER FOREIGN DATA WRAPPER useless OWNER TO regress_priv_user2; -- should fail
GRANT yb_fdw TO regress_priv_user2; 
ALTER FOREIGN DATA WRAPPER useless OWNER TO regress_priv_user2;
CREATE SERVER s1 FOREIGN DATA WRAPPER useless; -- should fail, since the owner changed
SET SESSION AUTHORIZATION regress_priv_user2;
CREATE SERVER s1 FOREIGN DATA WRAPPER useless;
ALTER SERVER s1 OPTIONS (host 'foo', dbname 'foodb');
DROP SERVER s1;
DROP FOREIGN DATA WRAPPER useless;

SET SESSION AUTHORIZATION regress_priv_user1;
CREATE EXTENSION file_fdw;
CREATE SERVER s1 FOREIGN DATA WRAPPER file_fdw;
-- should not allow the user to set the filename/program options as they require more privileges
CREATE FOREIGN TABLE test (x int) SERVER s1 OPTIONS ( filename 'foo');
CREATE FOREIGN TABLE test (x int) SERVER s1 OPTIONS ( program 'foo');
DROP SERVER s1;
DROP EXTENSION file_fdw;

\c -
DROP USER regress_priv_user1;
DROP USER regress_priv_user2;

-- test password change with BYPASSRLS attribute
CREATE ROLE user1 BYPASSRLS;
ALTER ROLE user1 PASSWORD 'password';
DROP ROLE user1;

--
-- Test yb_db_admin role
--
-- verify yb_db_admin role can change bypassrls attribute
CREATE ROLE regress_test_user1;
SET SESSION ROLE yb_db_admin;
ALTER ROLE regress_test_user1 WITH BYPASSRLS;
CREATE ROLE regress_test_user2 WITH BYPASSRLS;
ALTER ROLE regress_test_user2 WITH NOBYPASSRLS;

-- clean up
SET SESSION ROLE yugabyte;
DROP ROLE regress_test_user1;
DROP ROLE regress_test_user2;

--
-- Test YB Managed admin role
--
RESET SESSION AUTHORIZATION;
CREATE USER regress_priv_user;
GRANT yb_extension TO regress_priv_user;
GRANT yb_fdw TO regress_priv_user;
GRANT yb_db_admin TO regress_priv_user WITH ADMIN OPTION;
SET SESSION AUTHORIZATION regress_priv_user;
CREATE EXTENSION PGAudit;
ALTER EXTENSION PGAudit UPDATE TO '1.3.2';
DROP EXTENSION PGAudit;
CREATE EXTENSION orafce;
ALTER EXTENSION orafce UPDATE TO '3.14';
DROP EXTENSION orafce;
-- removing yb_db_admin role should result in error
REVOKE yb_db_admin FROM regress_priv_user;
CREATE EXTENSION PGAudit;
DROP EXTENSION PGAudit;
