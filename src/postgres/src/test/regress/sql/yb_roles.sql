-- test yb_extension role
CREATE USER regress_priv_user;
SET SESSION AUTHORIZATION regress_priv_user;
CREATE EXTENSION pgcrypto; -- should fail
\c -
GRANT yb_extension TO regress_priv_user;
SET SESSION AUTHORIZATION regress_priv_user;
CREATE EXTENSION pgcrypto;
ALTER EXTENSION pgcrypto UPDATE TO '1.3';
DROP EXTENSION pgcrypto;
\c -
DROP USER regress_priv_user;

-- test yb_fdw role
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
